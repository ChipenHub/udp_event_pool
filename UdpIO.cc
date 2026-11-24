/**
 * @file UdpIO.cc
 * @brief UdpIO类的实现文件 - epoll事件驱动的UDP IO处理
 *
 * 该文件包含UdpIO类所有成员函数的实现，包括：
 * - epoll事件循环的管理
 * - UDP socket的创建和管理
 * - 非阻塞UDP数据收发
 * - 多端口监听支持
 */

#include "UdpIO.h"
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <sys/socket.h>

namespace udp_framework {

/**
 * @brief UdpIO构造函数
 *
 * 初始化UdpIO对象，创建epoll实例准备事件监听。
 */
UdpIO::UdpIO()
    : epoll_fd_(-1),
      running_(false) {

    // 创建 epoll 实例
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        std::cerr << "Failed to create epoll fd: " << strerror(errno) << std::endl;
    }
}

UdpIO::~UdpIO() {
    stop();
    
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

/**
 * @brief 添加监听端口
 * @param port 要监听的端口号
 * @param ip 要绑定的IP地址
 * @return true如果添加成功，false如果失败
 */
bool UdpIO::add_listen_port(uint16_t port, const std::string& ip) {
    if (port_to_fd_.find(port) != port_to_fd_.end()) {
        std::cerr << "Port " << port << " already listening" << std::endl;
        return false;
    }

    int fd = create_udp_socket(port, ip);
    if (fd < 0) {
        return false;
    }

    // 添加到 epoll 监听
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;  // 边缘触发模式
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        std::cerr << "Failed to add fd to epoll: " << strerror(errno) << std::endl;
        close(fd);
        return false;
    }

    // 记录映射关系
    fd_to_port_[fd] = port;
    port_to_fd_[port] = fd;

    std::cout << "Added listen port: " << ip << ":" << port << " (fd=" << fd << ")" << std::endl;
    return true;
}

/**
 * @brief 启动IO循环
 */
void UdpIO::start() {
    if (running_.load()) {
        return;
    }

    if (port_to_fd_.empty()) {
        std::cerr << "No listening ports added" << std::endl;
        return;
    }

    running_.store(true);
    io_thread_ = std::thread([this] {
        this->io_loop();
    });

    std::cout << "UdpIO started with " << port_to_fd_.size() << " listening ports" << std::endl;
}

void UdpIO::stop() {
    if (!running_.load()) {
        return;
    }

    running_.store(false);

    if (io_thread_.joinable()) {
        io_thread_.join();
    }

    close_all_sockets();
    std::cout << "UdpIO stopped" << std::endl;
}

std::vector<uint16_t> UdpIO::get_listen_ports() const {
    std::vector<uint16_t> ports;
    for (const auto& pair : port_to_fd_) {
        ports.push_back(pair.first);
    }
    return ports;
}

int UdpIO::get_socket_fd(uint16_t port) const {
    auto it = port_to_fd_.find(port);
    return (it != port_to_fd_.end()) ? it->second : -1;
}

/**
 * @brief IO循环线程函数
 *
 * 运行epoll事件循环，监听和处理UDP事件。
 */
void UdpIO::io_loop() {
    std::vector<struct epoll_event> events(MAX_EPOLL_EVENTS);

    while (running_.load()) {
        // epoll_wait，超时时间 100ms
        int nfds = epoll_wait(epoll_fd_, events.data(), MAX_EPOLL_EVENTS, 100);

        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "epoll_wait error: " << strerror(errno) << std::endl;
            break;
        }

        // 处理所有就绪的事件
        for (int i = 0; i < nfds; ++i) {
            if (events[i].events & EPOLLIN) {
                handle_receive(events[i].data.fd);
            }
        }
    }
}

/**
 * @brief 创建UDP socket
 * @param port 要绑定的端口
 * @param ip 要绑定的IP地址
 * @return 创建的socket文件描述符，失败返回-1
 */
int UdpIO::create_udp_socket(uint16_t port, const std::string& ip) {
    // 创建 UDP socket
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return -1;
    }

    // 设置为非阻塞模式
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::cerr << "Failed to set non-blocking: " << strerror(errno) << std::endl;
        close(fd);
        return -1;
    }

    // 设置地址重用
    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "Failed to set SO_REUSEADDR: " << strerror(errno) << std::endl;
        close(fd);
        return -1;
    }

    // 绑定地址和端口
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "Invalid IP address: " << ip << std::endl;
        close(fd);
        return -1;
    }

    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to bind to " << ip << ":" << port << ": " << strerror(errno) << std::endl;
        close(fd);
        return -1;
    }

    return fd;
}

/**
 * @brief 处理接收事件
 * @param fd 发生接收事件的socket文件描述符
 */
void UdpIO::handle_receive(int fd) {
    // 循环接收数据，直到 EAGAIN（边缘触发模式）
    while (true) {
        struct sockaddr_in peer_addr;
        socklen_t addr_len = sizeof(peer_addr);
        char buf[UDP_BUFFER_SIZE];

        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0,
                            reinterpret_cast<struct sockaddr*>(&peer_addr),
                            &addr_len);

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 数据已读完
                break;
            }
            std::cerr << "recvfrom error: " << strerror(errno) << std::endl;
            break;
        } else if (n == 0) {
            // UDP 不会返回 0，但为了安全起见
            break;
        }

        // 封装数据
        Endpoint peer(peer_addr);

        // 获取本地端口
        uint16_t local_port = fd_to_port_[fd];

        // 调用回调函数（直接传递原始数据指针）
        if (recv_callback_) {
            recv_callback_(peer, buf, n, local_port);
        }
    }
}

void UdpIO::close_all_sockets() {
    for (const auto& pair : port_to_fd_) {
        close(pair.second);
    }
    fd_to_port_.clear();
    port_to_fd_.clear();
}

}  // namespace udp_framework


