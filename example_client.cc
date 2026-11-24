/**
 * @file example_client.cc
 * @brief UDP框架客户端示例 - 演示如何发送测试数据包
 *
 * 该客户端示例展示了如何构造不同类型的UDP数据包，
 * 并测试服务器的各种业务处理功能和优先级调度。
 */

#include "Endpoint.h"
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstring>

using namespace udp_framework;

/**
 * @brief 发送测试数据包到服务器
 * @param server_ip 服务器IP地址
 * @param server_port 服务器端口
 * @param packet_id 数据包ID
 * @param data 要发送的数据内容
 */
void send_test_packet(const std::string& server_ip, uint16_t server_port, 
                     uint32_t packet_id, const std::string& data) {
    // 创建 socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return;
    }

    // 设置接收超时
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // 构造数据包：[4字节 packet_id] + [数据]
    uint32_t network_packet_id = htonl(packet_id);
    std::string packet;
    packet.append(reinterpret_cast<const char*>(&network_packet_id), sizeof(network_packet_id));
    packet.append(data);

    // 目标地址
    Endpoint server(server_ip, server_port);

    // 发送数据
    ssize_t sent = sendto(sockfd, packet.data(), packet.size(), 0,
                         server.get_sockaddr(), server.length());
    
    if (sent < 0) {
        std::cerr << "Failed to send data" << std::endl;
        close(sockfd);
        return;
    }

    std::cout << "Sent " << sent << " bytes to " << server.to_string() 
              << " (packet_id=" << packet_id << ", data=\"" << data << "\")" << std::endl;

    // 接收回复（仅对 Echo 服务有效）
    if (packet_id == 1) {
        char recv_buf[65536];
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        
        ssize_t n = recvfrom(sockfd, recv_buf, sizeof(recv_buf), 0,
                            reinterpret_cast<struct sockaddr*>(&from_addr),
                            &from_len);
        
        if (n > 0) {
            Endpoint from(from_addr);
            std::cout << "Received " << n << " bytes from " << from.to_string() << std::endl;
            std::cout << "Echo reply: " << std::string(recv_buf + 4, n - 4) << std::endl;
        } else {
            std::cout << "No reply received (timeout or error)" << std::endl;
        }
    }

    close(sockfd);
}

/**
 * @brief 主函数 - 执行客户端测试流程
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return 程序退出码
 */
int main(int argc, char* argv[]) {
    std::cout << "=== UDP Framework Example Client ===" << std::endl;

    std::string server_ip = "127.0.0.1";
    uint16_t server_port = 8001;

    if (argc >= 3) {
        server_ip = argv[1];
        server_port = static_cast<uint16_t>(std::atoi(argv[2]));
    }

    std::cout << "Target server: " << server_ip << ":" << server_port << std::endl;
    std::cout << std::endl;

    // 测试不同的 packet_id
    std::cout << "Test 1: Echo service (packet_id=1)" << std::endl;
    send_test_packet(server_ip, server_port, 1, "Hello, Echo!");
    std::cout << std::endl;
    sleep(1);

    std::cout << "Test 2: Stats service (packet_id=2)" << std::endl;
    send_test_packet(server_ip, server_port, 2, "Stats data 1");
    sleep(1);
    send_test_packet(server_ip, server_port, 2, "Stats data 2");
    std::cout << std::endl;
    sleep(1);

    std::cout << "Test 3: Critical service (packet_id=3)" << std::endl;
    send_test_packet(server_ip, server_port, 3, "Critical message!");
    std::cout << std::endl;
    sleep(1);

    std::cout << "Test 4: Unknown packet_id (packet_id=999)" << std::endl;
    send_test_packet(server_ip, server_port, 999, "Unknown packet type");
    std::cout << std::endl;
    sleep(1);

    std::cout << "Test 5: Multiple Echo requests" << std::endl;
    for (int i = 0; i < 5; ++i) {
        std::string msg = "Echo test " + std::to_string(i + 1);
        send_test_packet(server_ip, server_port, 1, msg);
        sleep(1);
    }

    std::cout << "\nAll tests completed." << std::endl;
    return 0;
}


