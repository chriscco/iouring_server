#pragma once
#include <coroutine>
#include <stdlib.h>

enum Task_Option {
    ACCEPT,
    READ,
    WRITE,
    OPEN,
    PROV_BUF,
};

union request {
    struct {
        short event_type;
        short bid;
        int client_sockfd;
    };
    unsigned long long uring_data;
};

class IO_Uring_Handler;
class Task {
public:
    struct promise_type {
        using Handle = std::coroutine_handle<promise_type>;

        /**
         * 返回协程的句柄，允许调用方通过句柄与携程交互
         */
        Task get_return_object() {
            return Task{Handle::from_promise(*this)};
        } 
        /* 协程启动后永远都会挂起 */
        std::suspend_always initial_suspend() noexcept {
            return {};
        }
        /* 协程结束后不会挂起 */
        std::suspend_never final_suspend() noexcept {
            return {};
        }
        void return_void() noexcept {};
        void unhandled_exception() noexcept {};

        request request_info;
        IO_Uring_Handler* uring;
        size_t res;
    };

    Task() : _handler(nullptr) {}
    Task(promise_type::Handle handler) : _handler(handler) {};
    void destroy() { _handler.destroy(); }
    Task(Task const &) = delete;
    Task& operator=(Task const &) = delete;
    Task(Task&& that) noexcept : _handler(that._handler) {
        that._handler = nullptr;
    }
    Task& operator=(Task&& that) noexcept {
        if (this == &that) {
            return *this;
        }
        if (_handler) {
            _handler.destroy();
        }
        _handler = that._handler;
        that._handler = nullptr;
        return *this;
    }

    promise_type::Handle _handler;
};