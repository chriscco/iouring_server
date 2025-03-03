cmake_minimum_required(VERSION 3.10)
project(iouring_server)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing)

include_directories(${LIBURING_INCLUDE_DIRS})
target_link_libraries(server ${LIBURING_LIBRARIES})

set(CMAKE_CXX_STANDARD 20)
include_directories(${PROJECT_DIR}/)

add_executable(server main.cpp)
