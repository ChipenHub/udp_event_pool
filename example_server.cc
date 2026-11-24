/**
 * @file example_server.cc
 * @brief UDP框架服务器示例 - 演示完整的UDP服务器实现
 *
 * 该示例展示了如何使用UDP框架构建一个支持优先级调度的服务器，
 * 包括多端口监听、业务回调注册、信号处理等完整功能。
 */

#include "UdpIO.h"
#include "Dispatcher.h"
#include "ThreadPool.h"
#include "Endpoint.h"
#include "Buffer.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>
#include <memory>

using namespace udp_framework;

// 全局变量，用于信号处理
static std::shared_ptr<UdpIO> g_udp_io;

// 信号处理函数
void signal_handler(int sig) {
    std::cout << "\nReceived signal " << sig << ", shutting down..." << std::endl;
    if (g_udp_io) {
        g_udp_io->stop();
    }
}

/**
 * @brief Echo服务业务回调 - 演示基本的请求响应模式
 * @param peer 客户端端点
 * @param buffer 接收到的数据缓冲区
 * @param local_port 本地接收端口
 */
void handle_echo(const Endpoint& peer, muduo::net::Buffer* buffer, uint16_t local_port) {
    std::string data = buffer->retrieveAllAsString();
    std::cout << "[ECHO] Received " << data.size() << " bytes from " 
              << peer.to_string() << " on port " << local_port << std::endl;
    std::cout << "[ECHO] Data: " << data << std::endl;

    // Echo 回传（业务层直接使用sendto系统调用）
    if (g_udp_io) {
        int fd = g_udp_io->get_socket_fd(local_port);
        if (fd >= 0) {
            ssize_t n = sendto(fd, data.data(), data.size(), 0,
                             peer.get_sockaddr(), peer.length());
            if (n >= 0) {
                std::cout << "[ECHO] Sent " << n << " bytes echo reply to " 
                         << peer.to_string() << std::endl;
            } else {
                std::cerr << "[ECHO] sendto failed: " << strerror(errno) << std::endl;
            }
        } else {
            std::cerr << "[ECHO] Socket fd not found for port " << local_port << std::endl;
        }
    }
}

/**
 * @brief 统计服务业务回调 - 演示数据统计和记录功能
 * @param peer 客户端端点
 * @param buffer 接收到的数据缓冲区
 * @param local_port 本地接收端口
 */
void handle_stats(const Endpoint& peer, muduo::net::Buffer* buffer, uint16_t local_port) {
    static int packet_count = 0;
    packet_count++;
    
    std::string data = buffer->retrieveAllAsString();
    std::cout << "[STATS] Packet #" << packet_count << " from " 
              << peer.to_string() << " on port " << local_port << std::endl;
    std::cout << "[STATS] Size: " << data.size() << " bytes" << std::endl;
}

/**
 * @brief 高优先级业务回调 - 演示优先级调度的应用
 * @param peer 客户端端点
 * @param buffer 接收到的数据缓冲区
 * @param local_port 本地接收端口
 */
void handle_critical(const Endpoint& peer, muduo::net::Buffer* buffer, uint16_t local_port) {
    std::string data = buffer->retrieveAllAsString();
    std::cout << "[CRITICAL] High priority packet from " 
              << peer.to_string() << " on port " << local_port << std::endl;
    std::cout << "[CRITICAL] Data: " << data << std::endl;
}

/**
 * @brief 默认业务回调 - 处理未注册的packet_id
 * @param peer 客户端端点
 * @param buffer 接收到的数据缓冲区
 * @param local_port 本地接收端口
 */
void handle_default(const Endpoint& peer, muduo::net::Buffer* buffer, uint16_t local_port) {
    std::string data = buffer->retrieveAllAsString();
    std::cout << "[DEFAULT] Unknown packet type from " 
              << peer.to_string() << " on port " << local_port << std::endl;
    std::cout << "[DEFAULT] Data: " << data << std::endl;
}

/**
 * @brief 主函数 - 演示UDP框架的完整使用流程
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return 程序退出码
 */
int main(int argc, char* argv[]) {
    std::cout << "=== UDP Framework Example Server (Multi-Group) ===" << std::endl;
    std::cout << "Usage: " << argv[0] << " [port1] [port2] ..." << std::endl;
    std::cout << std::endl;

    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 1. 创建线程池（分组模式）
    // Group 0: 3个线程处理普通业务
    // Group 1: 2个线程处理关键业务
    // Group 2: 2个线程处理统计业务
    std::vector<size_t> group_configs = {3, 2, 2};
    auto thread_pool = std::make_shared<ThreadPool>(group_configs);
    thread_pool->start();
    std::cout << "ThreadPool started with " << thread_pool->group_count() << " groups, "
              << thread_pool->thread_count() << " threads total" << std::endl;
    for (size_t i = 0; i < thread_pool->group_count(); ++i) {
        std::cout << "  Group " << i << ": " << thread_pool->group_thread_count(i) << " threads" << std::endl;
    }

    // 2. 创建 Dispatcher
    auto dispatcher = std::make_shared<Dispatcher>(thread_pool);

    // 3. 注册业务回调（新接口 - 指定线程组）
    // packet_id = 1: Echo 服务 → Group 0（普通业务组）
    dispatcher->register_handler(1, handle_echo, 0, Task::NORMAL);
    
    // packet_id = 2: 统计服务 → Group 2（统计业务组）
    dispatcher->register_handler(2, handle_stats, 2, Task::LOW);
    
    // packet_id = 3: 关键业务 → Group 1（关键业务组）
    dispatcher->register_handler(3, handle_critical, 1, Task::CRITICAL);

    // 设置默认处理函数（使用 Group 0）
    dispatcher->set_default_handler(handle_default);

    // 4. 创建 UdpIO
    g_udp_io = std::make_shared<UdpIO>();

    // 5. 添加监听端口
    std::vector<uint16_t> ports;
    if (argc > 1) {
        // 使用命令行参数指定的端口
        for (int i = 1; i < argc; ++i) {
            uint16_t port = static_cast<uint16_t>(std::atoi(argv[i]));
            ports.push_back(port);
        }
    } else {
        // 默认监听端口
        ports = {8001, 8002, 8003};
    }

    for (uint16_t port : ports) {
        if (!g_udp_io->add_listen_port(port)) {
            std::cerr << "Failed to add listen port: " << port << std::endl;
            return 1;
        }
    }

    // 6. 设置接收回调（将 IO 层和 Dispatcher 连接起来）
    g_udp_io->set_recv_callback([dispatcher](const Endpoint& peer, 
                                             const char* data,
                                             size_t len,
                                             uint16_t local_port) {
        dispatcher->on_receive(peer, data, len, local_port);
    });

    // 7. 启动 IO 循环
    g_udp_io->start();

    std::cout << "\n=== Server is running ===" << std::endl;
    std::cout << "Listening on ports: ";
    for (uint16_t port : ports) {
        std::cout << port << " ";
    }
    std::cout << std::endl;
    std::cout << "\nPacket format: [4-byte packet_id (network byte order)] + [data]" << std::endl;
    std::cout << "  packet_id = 1: Echo service (Group 0 - Normal)" << std::endl;
    std::cout << "  packet_id = 2: Stats service (Group 2 - Low)" << std::endl;
    std::cout << "  packet_id = 3: Critical service (Group 1 - Critical)" << std::endl;
    std::cout << "\nThread Group Info:" << std::endl;
    std::cout << "  Group 0: 3 threads for normal business" << std::endl;
    std::cout << "  Group 1: 2 threads for critical business" << std::endl;
    std::cout << "  Group 2: 2 threads for stats business" << std::endl;
    std::cout << "\nPress Ctrl+C to stop..." << std::endl;

    // 主线程等待
    while (g_udp_io->is_running()) {
        sleep(1);
    }

    // 8. 清理
    std::cout << "\nShutting down..." << std::endl;
    thread_pool->stop();
    
    std::cout << "Server stopped." << std::endl;
    return 0;
}


