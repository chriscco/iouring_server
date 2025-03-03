#pragma once
#include <linux/io_uring.h>
#include <liburing.h>
#include <cstring>
#include <sys/socket.h>
#include <unordered_map>
#include <memory>
#include <netinet/in.h>
#include "task.h"
#include "utils.h"

constexpr size_t MAX_MESSAGE_LEN = 2048;
constexpr size_t BUFFER_COUNT = 4096;

class IO_Uring_Handler {
public:
    IO_Uring_Handler(unsigned, int);
    void event_loop(Task func(int));
    void setup_first_buffer();
    ~IO_Uring_Handler();
    void add_read_request(int, request&);
    void add_write_request(int, size_t, request&);
    void add_accept_request(int fd, struct sockaddr*, socklen_t*, unsigned);
    void add_buffer_request(request&);
    void add_open_request();
    void add_close_request(int);

    char* get_buffer_pointer(int bid) {
        return _buffer[bid];
    }
    int get_buffer_id(char* buffer) {
        return (buffer - (char*)this->_buffer.get()) / MAX_MESSAGE_LEN;
    }
private:
    struct io_uring _ring;
    std::unique_ptr<char[][MAX_MESSAGE_LEN]> _buffer;
    std::unordered_map<int, Task> _connections;
    struct sockaddr_in _client;
    socklen_t _socklen = sizeof(_client);
    int _sock_fd; // for client
};

/**
 * @brief 
 * @param entries io uring的队列深度，即能同时处理的最大IO请求数
 */
IO_Uring_Handler::IO_Uring_Handler(unsigned entries, int sock_fd) {
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));
    this->_sock_fd = sock_fd;

    if (io_uring_queue_init_params(entries, &_ring, &params) < 0) {
        errif(true, "error in io)uring_queue_init_params");
    }

    /**
     * Fast Poll：它在资源可用时立即执行 I/O 操作，
     * 不创建额外的浪费 CPU 时间的线程，
     * 因此适用于那些 I/O 频繁阻塞的场景。
     * 它在每次提交时都会进行上下文切换
     * 
     * SQPoll：将IO提交交由内核线程收集，避免了上下文切换，
     * 但是在没有任务时线程会闲置
     */
    if (!(params.features & IORING_FEAT_FAST_POLL)) {
        printf("IORING_FEAT_FAST_POLL is not supported\n");
        exit(0);
    }

    /**
     * probe用于验证内核是否支持某些操作
     */
    struct io_uring_probe* probe;
    probe = io_uring_get_probe_ring(&_ring);
    if (!probe || 
        !io_uring_opcode_supported(probe, IORING_OP_PROVIDE_BUFFERS)) {
        printf("buffer not supported");
        exit(0);
    }
    free(probe);

    setup_first_buffer();
}

IO_Uring_Handler::~IO_Uring_Handler() {
    io_uring_queue_exit(&_ring);
}

void IO_Uring_Handler::setup_first_buffer() {
    _buffer.reset(new char[BUFFER_COUNT][MAX_MESSAGE_LEN]);

    struct io_uring_sqe* sqe;
    struct io_uring_cqe* cqe;

    sqe = io_uring_get_sqe(&_ring);
    io_uring_prep_provide_buffers(sqe, _buffer.get(), 
        MAX_MESSAGE_LEN, BUFFER_COUNT, group_id, 0);

    io_uring_submit(&_ring);
    io_uring_wait_cqe(&_ring, &cqe);
    
    errif(cqe->res < 0, "cqe->res < 0");

    io_uring_queue_exit(&_ring);
}

void IO_Uring_Handler::event_loop(Task handle_event(int)) {
    printf("start event loop\n");
    add_accept_request(_sock_fd, (struct sockaddr*)&_client, &_socklen, 0);
    while (true) {
        /* 等待1个min_completion完成 */
        io_uring_submit_and_wait(&_ring, 1);
        struct io_uring_cqe* cqe;
        unsigned int head, count = 0;

        /* 遍历cqe */
        io_uring_for_each_cqe(&_ring, head, cqe) {
            count++;
            request conn_i;
            memcpy(&conn_i, &cqe->user_data, sizeof(conn_i));

            int type = conn_i.event_type;
            if (cqe->res == -ENOBUFS) {
                fprintf(stdout, "buffer is empty");
                fflush(stdout);
                exit(1);
            } else if (type = PROV_BUF) {
                errif(cqe->res < 0, "cqe->res < 0");
            } else if (type == ACCEPT) {
                int sock_conn_fd = cqe->res;
                
                printf("accept in io uring for each cqe");
                if (sock_conn_fd >= 0) {
                    _connections.emplace(sock_conn_fd, handle_event(sock_conn_fd));
                    auto& handler = _connections[sock_conn_fd]._handler;
                    auto& promise = handler.promise();
                    promise.request_info.client_sockfd = sock_conn_fd;
                    promise.uring = this;
                    handler.resume();
                }
                /* 新连接的客户端，从fd中读取信息 */
                add_accept_request(_sock_fd, (struct sockaddr*)&_client, &_socklen, 0);
            } else if (type = READ) {
                auto& handler = _connections[conn_i.client_sockfd]._handler;
                auto& promise = handler.promise();
                promise.request_info.bid = cqe->flags >> 16;
                promise.res = cqe->res;
                handler.resume();
            } else if (type == WRITE) {
                auto& handler = _connections[conn_i.client_sockfd]._handler;
                handler.promise().res = cqe->res;
                handler.resume();
            }
        }
        /* 根据设置的min_completion从完成队列中取回结果 */
        io_uring_cq_advance(&_ring, count);
    }
}

void IO_Uring_Handler::add_read_request(int fd, request& req) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&_ring);
    io_uring_prep_recv(sqe, fd, NULL, MAX_MESSAGE_LEN, 0);
    io_uring_sqe_set_flags(sqe, IOSQE_BUFFER_SELECT);
    sqe->buf_group = group_id;
    req.event_type = READ;
    sqe->user_data = req.uring_data;
}

void IO_Uring_Handler::add_close_request(int fd) {
    shutdown(fd, SHUT_RDWR);
    _connections.erase(fd);
}

void IO_Uring_Handler::add_write_request(int fd, size_t message_size, 
                                request& req) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&_ring);
    io_uring_prep_send(sqe, fd, &_buffer[req.bid], message_size, 0);
    io_uring_sqe_set_flags(sqe, 0);
    req.event_type = WRITE;
    sqe->user_data = req.uring_data;
    printf("add write req: %lu\n", message_size);
}

void IO_Uring_Handler::add_accept_request(int fd, struct sockaddr* client_addr, 
        socklen_t* client_len, unsigned flags) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&_ring);
    io_uring_prep_accept(sqe, fd, client_addr, client_len, 0);
    io_uring_sqe_set_flags(sqe, flags);

    request conn_i;
    conn_i.event_type = ACCEPT;
    conn_i.bid = 0;
    conn_i.client_sockfd = fd;

    sqe->user_data = conn_i.uring_data;
}

void IO_Uring_Handler::add_buffer_request(request& req) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&_ring);
    io_uring_prep_provide_buffers(sqe, 
            _buffer[req.bid], MAX_MESSAGE_LEN, 1, group_id, 0);
    req.event_type = PROV_BUF;
    sqe->user_data = req.uring_data;
}

void IO_Uring_Handler::add_open_request() {};

