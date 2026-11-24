cmake_minimum_required(VERSION 3.5)
project(UdpFramework)

# 设置 C++11 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 框架核心库源文件
set(FRAMEWORK_SOURCES
    Buffer.cc
    Task.cc
    UdpIO.cc
    Dispatcher.cc
)

# 创建框架静态库
add_library(udp_framework STATIC ${FRAMEWORK_SOURCES})
target_link_libraries(udp_framework pthread)

# 服务器可执行文件
add_executable(udp_server example_server.cc)
target_link_libraries(udp_server udp_framework pthread)

# 客户端可执行文件
add_executable(udp_client example_client.cc)
target_link_libraries(udp_client udp_framework pthread)

