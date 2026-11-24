/**
 * @file Endpoint.h
 * @brief UDP端点封装类 - 表示UDP通信的对端地址信息
 *
 * 该文件定义了Endpoint类，用于封装UDP通信中的IP地址和端口信息。
 * 提供地址的存储、转换、比较等操作，便于网络编程。
 */

#ifndef UDP_ENDPOINT_H
#define UDP_ENDPOINT_H

#include <string>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>

namespace udp_framework {

/**
 * @class Endpoint
 * @brief UDP端点类 - 封装IP地址和端口信息
 *
 * Endpoint类封装了UDP通信中对端的信息，包括IP地址和端口号。
 * 提供地址的各种操作接口，支持作为map的key使用。
 */
class Endpoint {
public:
    /**
     * @brief 默认构造函数
     *
     * 创建一个空的端点，地址为0.0.0.0:0
     */
    Endpoint() {
        std::memset(&addr_, 0, sizeof(addr_));
        addr_.sin_family = AF_INET;
    }

    /**
     * @brief 构造函数 - 从IP字符串和端口号创建
     * @param ip IP地址字符串（如"192.168.1.1"）
     * @param port 端口号（主机字节序）
     */
    Endpoint(const std::string& ip, uint16_t port) {
        std::memset(&addr_, 0, sizeof(addr_));
        addr_.sin_family = AF_INET;
        addr_.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr_.sin_addr);
    }

    /**
     * @brief 构造函数 - 从sockaddr_in结构体创建
     * @param addr sockaddr_in结构体
     */
    explicit Endpoint(const struct sockaddr_in& addr) : addr_(addr) {}

    /**
     * @brief 获取IP地址字符串
     * @return IP地址字符串（如"192.168.1.1"）
     */
    std::string ip() const {
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf));
        return std::string(buf);
    }

    /**
     * @brief 获取端口号
     * @return 端口号（主机字节序）
     */
    uint16_t port() const {
        return ntohs(addr_.sin_port);
    }

    /**
     * @brief 获取sockaddr_in结构体的常量引用
     * @return sockaddr_in结构体的常量引用
     */
    const struct sockaddr_in& sockaddr() const {
        return addr_;
    }

    /**
     * @brief 获取sockaddr_in结构体的引用
     * @return sockaddr_in结构体的引用
     */
    struct sockaddr_in& sockaddr() {
        return addr_;
    }

    /**
     * @brief 获取sockaddr指针（用于系统调用）
     * @return sockaddr结构体的常量指针
     */
    const struct sockaddr* get_sockaddr() const {
        return reinterpret_cast<const struct sockaddr*>(&addr_);
    }

    /**
     * @brief 获取sockaddr指针（用于系统调用）
     * @return sockaddr结构体的指针
     */
    struct sockaddr* get_sockaddr() {
        return reinterpret_cast<struct sockaddr*>(&addr_);
    }

    /**
     * @brief 获取地址结构长度
     * @return 地址结构的字节长度
     */
    socklen_t length() const {
        return sizeof(addr_);
    }

    /**
     * @brief 转换为字符串表示
     * @return "IP:端口"格式的字符串
     */
    std::string to_string() const {
        return ip() + ":" + std::to_string(port());
    }

    /**
     * @brief 小于比较运算符
     * @param other 要比较的另一个Endpoint
     * @return true如果当前端点小于other
     *
     * 首先比较IP地址，然后比较端口号。用于作为map的key。
     */
    bool operator<(const Endpoint& other) const {
        if (addr_.sin_addr.s_addr != other.addr_.sin_addr.s_addr) {
            return addr_.sin_addr.s_addr < other.addr_.sin_addr.s_addr;
        }
        return addr_.sin_port < other.addr_.sin_port;
    }

    /**
     * @brief 等于比较运算符
     * @param other 要比较的另一个Endpoint
     * @return true如果两个端点完全相同
     */
    bool operator==(const Endpoint& other) const {
        return addr_.sin_addr.s_addr == other.addr_.sin_addr.s_addr &&
               addr_.sin_port == other.addr_.sin_port;
    }

private:
    struct sockaddr_in addr_;
};

}  // namespace udp_framework

#endif  // UDP_ENDPOINT_H


