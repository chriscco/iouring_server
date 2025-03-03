#pragma once
#include <coroutine>
#include "io_uring.h"

struct stream_base {
    stream_base(Task::promise_type* promise, size_t message_size) 
            : _promise(promise), _m_size(message_size) {};
    Task::promise_type* _promise = nullptr;
    size_t _m_size;
};


struct read_awaitable : public stream_base {
    read_awaitable(Task::promise_type* promise, size_t message_size, char** buffer_ptr) 
                : stream_base(promise, message_size),
                _buffer_ptr(buffer_ptr) {}
    bool await_ready() { return false; }
    /** 
     * 在 await_ready() 返回 false 时执行
     */
    void await_suspend(std::coroutine_handle<Task::promise_type> handler) {
        auto& promise = handler.promise();
        this->_promise = &promise;
        promise.uring->add_read_request(promise.request_info.client_sockfd, 
                promise.request_info);
    }
    size_t await_resume() {
        *_buffer_ptr = _promise->uring->get_buffer_pointer(_promise->request_info.bid);
        return _promise->res;
    }
    char** _buffer_ptr;
};

struct write_awaitable : public stream_base {
    write_awaitable(Task::promise_type* promise, size_t message_size, char* buffer)
        : stream_base(promise, message_size),
        _buffer(buffer) {}
    
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<Task::promise_type> handler) {
        auto& promise = handler.promise();
        this->_promise = &promise;
        promise.request_info.bid = promise.uring->get_buffer_id(_buffer);
        promise.uring->add_write_request(promise.request_info.client_sockfd, 
            _m_size, promise.request_info);
    }

    size_t await_resume() {
        _promise->uring->add_buffer_request(_promise->request_info);
        return _promise->res;
    }

    char* _buffer;
};

struct read_file_awaitable : public read_awaitable {   
    read_file_awaitable(Task::promise_type *promise, size_t message_size, char **buffer_pointer, int read_fd)
        : read_awaitable(promise, message_size, buffer_pointer),
          read_fd(read_fd) {}
    void await_suspend(std::coroutine_handle<Task::promise_type> h) {
        auto &promise = h.promise();
        this->_promise = &promise;
        promise.uring->add_read_request(read_fd, promise.request_info);
    }
    int read_fd;
};

struct write_file_awaitable : public write_awaitable
{
    write_file_awaitable(Task::promise_type *promise, size_t message_size, char *buffer, int write_fd)
        : write_awaitable(promise, message_size, buffer),
          _write_fd(write_fd) {}
    void await_suspend(std::coroutine_handle<Task::promise_type> h)
    {
        auto &promise = h.promise();
        this->_promise = &promise;
        promise.request_info.bid = promise.uring->get_buffer_id(_buffer);
        promise.uring->add_write_request(_write_fd, _m_size, promise.request_info);
    }
    int _write_fd;
};

struct close_awaitable {   
    close_awaitable(int fd): _fd(fd) {};
    bool await_ready() { return true; }
    void await_suspend(std::coroutine_handle<Task::promise_type> h) {
        auto &promise = h.promise();
        promise.uring->add_close_request(_fd);
    }
    void await_resume() {}
    int _fd;
};

auto read_socket(char **buffer_pointer) {
    return read_awaitable(nullptr, 0, buffer_pointer);
}

auto read_fd(int fd, char **buffer_pointer) {
    return read_file_awaitable(nullptr, 0, buffer_pointer, fd);
}

auto write_fd(int fd, char *buffer, size_t message_size) {
    return write_file_awaitable(nullptr, message_size, buffer, fd);
}

auto write_socket(char *buffer, size_t message_size) {   
    return write_awaitable(nullptr, message_size, buffer);
}

auto shutdown_socket(int fd) {
    return close_awaitable(fd);
}