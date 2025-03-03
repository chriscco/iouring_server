#pragma once
#include <coroutine> 

#include "io_uring.h"
#include "httpconn.h"

constexpr size_t ENTRIES = 2048;

class Server {
public: 
    Server(int);
    ~Server();
    void start();
private:
    std::unique_ptr<IO_Uring_Handler> _uring;
    int _sockfd;

    void setup_listening_socket(int);
};

void Server::setup_listening_socket(int port) {
    struct sockaddr_in server_addr;

    _sockfd = socket(AF_INET, SOCK_STREAM, 0);
    errif(_sockfd == -1, "socket error");
    int enable = 1;
    errif(setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, 
        &enable, sizeof(int)) == -1, "error in setsockopt");
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    errif(bind(_sockfd, (const struct sockaddr*)&server_addr, sizeof(server_addr)),
            "error in bind");
    errif(listen(_sockfd, SOMAXCONN) == -1, "error in listen");
}

Server::Server(int port) {
    setup_listening_socket(port);
    _uring.reset(new IO_Uring_Handler(ENTRIES, _sockfd));
}
Server::~Server() {};

Task handle_http_request(int fd) {
    char* read_buffer;
    http_conn conn;

    size_t read_bytes = co_await read_socket(&read_buffer);
    read_buffer[read_bytes] = 0;

    conn.handle_request(read_buffer);
    size_t write_bytes = co_await write_socket(read_buffer, conn.get_response_size());
    printf("write_buffer %lu %s", write_bytes, read_buffer);
    co_await shutdown_socket(fd);
    co_return;
}

void Server::start() {
    _uring->event_loop(handle_http_request);
}