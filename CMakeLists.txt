cmake_minimum_required(VERSION 3.10)
project(iouring_server)


set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fcoroutines -luring")

include_directories(${PROJECT_DIR}/)
include_directories(/usr/include)
link_libraries(uring)

add_executable(server main.cpp)
add_executable(client client.cpp)

target_link_libraries(server uring)
