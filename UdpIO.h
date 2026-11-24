/**
 * @file UdpIO.h
 * @brief UDP IO层 - 基于epoll的多端口UDP监听和收发
 *
 * 该文件定义了UdpIO类，负责UDP网络通信的底层IO操作。
 * 使用epoll进行高效的事件驱动，支持多端口监听和异步收发。
 */

#ifndef UDP_IO_H
#define UDP_IO_H

#include "Endpoint.h"
#include <functional>
#include <map>
#include <vector>
#include <atomic>
#include <thread>
#include <memory>
#include <sys/epoll.h>

namespace udp_framework {

/**
 * @brief UDP接收回调函数类型
 * @param peer 对端端点（发送方）
 * @param data 接收到的数据指针
 * @param len 数据长度
 * @param local_port 本地接收端口
 */
using UdpRecvCallback = std::function<void(const Endpoint&, const char*, size_t, uint16_t)>;

/**
 * @class UdpIO
 * @brief UDP IO层类 - 基于epoll的事件驱动UDP通信
 *
 * UdpIO类负责UDP网络通信的底层IO操作，支持：
 * - 多端口监听（单进程监听多个UDP端口）
 * - epoll事件驱动（高效的IO多路复用）
 * - 非阻塞UDP收发
 * - 边缘触发模式（ET）
 */
class UdpIO {
public:
    /** @brief epoll最大事件数 */
    static const int MAX_EPOLL_EVENTS = 64;
    /** @brief UDP缓冲区最大大小 */
    static const int UDP_BUFFER_SIZE = 65536;  // UDP 最大包大小

    /**
     * @brief 构造函数
     *
     * 初始化UdpIO对象，准备epoll和相关资源。
     */
    UdpIO();

    /**
     * @brief 析构函数
     *
     * 清理所有资源，关闭socket和epoll。
     */
    ~UdpIO();

    // 禁止拷贝和赋值
    UdpIO(const UdpIO&) = delete;
    UdpIO& operator=(const UdpIO&) = delete;

    /**
     * @brief 添加监听端口
     * @param port 要监听的端口号
     * @param ip 要绑定的IP地址，默认"0.0.0.0"（所有接口）
     * @return true如果添加成功，false如果失败
     *
     * 为指定的IP和端口创建UDP socket并开始监听。
     */
    bool add_listen_port(uint16_t port, const std::string& ip = "0.0.0.0");

    /**
     * @brief 设置接收回调函数
     * @param callback 接收到UDP数据时的回调函数
     *
     * 设置处理接收到的UDP数据的回调函数。
     * 回调函数会在IO线程中被调用。
     */
    void set_recv_callback(const UdpRecvCallback& callback) {
        recv_callback_ = callback;
    }

    /**
     * @brief 启动IO循环
     *
     * 在独立的IO线程中启动epoll事件循环，开始监听和处理UDP数据。
     */
    void start();

    /**
     * @brief 停止IO循环
     *
     * 停止epoll事件循环，等待IO线程退出。
     */
    void stop();

    /**
     * @brief 获取监听端口列表
     * @return 当前监听的所有端口号列表
     */
    std::vector<uint16_t> get_listen_ports() const;

    /**
     * @brief 获取指定端口的socket文件描述符
     * @param port 端口号
     * @return socket fd，如果端口不存在返回-1
     *
     * 业务层可以使用返回的fd直接调用sendto系统调用发送数据
     */
    int get_socket_fd(uint16_t port) const;

    /**
     * @brief 检查是否正在运行
     * @return true如果IO循环正在运行
     */
    bool is_running() const {
        return running_.load();
    }

private:
    /**
     * @brief IO循环线程函数
     *
     * 在独立线程中运行epoll事件循环，监听和处理UDP事件。
     */
    void io_loop();

    /**
     * @brief 创建UDP socket
     * @param port 要绑定的端口
     * @param ip 要绑定的IP地址
     * @return 创建的socket文件描述符，失败返回-1
     *
     * 创建非阻塞UDP socket并绑定到指定地址。
     */
    int create_udp_socket(uint16_t port, const std::string& ip);

    /**
     * @brief 处理接收事件
     * @param fd 发生接收事件的socket文件描述符
     *
     * 处理指定socket上的UDP数据接收，循环读取所有待处理数据。
     */
    void handle_receive(int fd);

    /**
     * @brief 关闭所有socket
     *
     * 关闭所有监听socket并清理相关映射。
     */
    void close_all_sockets();

private:
    /** @brief epoll文件描述符 */
    int epoll_fd_;
    /** @brief IO循环运行状态 */
    std::atomic<bool> running_;
    /** @brief IO线程对象 */
    std::thread io_thread_;

    /** @brief socket fd到本地端口的映射 */
    std::map<int, uint16_t> fd_to_port_;

    /** @brief 本地端口到socket fd的映射 */
    std::map<uint16_t, int> port_to_fd_;

    /** @brief 接收数据回调函数 */
    UdpRecvCallback recv_callback_;
};

}  // namespace udp_framework

#endif  // UDP_IO_H


