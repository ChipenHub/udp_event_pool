/**
 * @file Dispatcher.cc
 * @brief Dispatcher类的实现文件 - 数据包分发和任务调度
 *
 * 该文件包含Dispatcher类所有成员函数的实现，负责：
 * - 业务回调的注册和注销
 * - 数据包头部的解析
 * - 任务的构造和提交
 */

#include "Dispatcher.h"
#include <iostream>
#include <arpa/inet.h>

namespace udp_framework {

/**
 * @brief 注册业务回调处理函数（新接口 - 指定线程组）
 */
void Dispatcher::register_handler(PacketId packet_id,
                                  BusinessCallback callback,
                                  GroupId group_id,
                                  Task::Priority priority) {
    // 检查group_id是否有效
    if (group_id < 0 || static_cast<size_t>(group_id) >= thread_pool_->group_count()) {
        std::cerr << "[Dispatcher] Invalid group_id " << group_id 
                  << " for packet_id " << packet_id << std::endl;
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[packet_id] = HandlerInfo(std::move(callback), group_id, priority);
    std::cout << "[Dispatcher] Registered handler for packet_id: " << packet_id 
              << ", group_id: " << group_id
              << ", priority: " << static_cast<int>(priority) << std::endl;
}

/**
 * @brief 注册业务回调处理函数（旧接口 - 兼容单组场景）
 */
void Dispatcher::register_handler(PacketId packet_id,
                                  BusinessCallback callback,
                                  Task::Priority priority) {
    if (thread_pool_->group_count() != 1) {
        std::cerr << "[Dispatcher] Multiple thread groups exist (" 
                  << thread_pool_->group_count() 
                  << "), must specify group_id. Use register_handler(packet_id, callback, group_id, priority)" 
                  << std::endl;
        return;
    }
    
    // 单组时默认使用group 0
    register_handler(packet_id, std::move(callback), 0, priority);
}

/**
 * @brief 注销业务回调处理函数
 */
void Dispatcher::unregister_handler(PacketId packet_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_.erase(packet_id);
    std::cout << "Unregistered handler for packet_id: " << packet_id << std::endl;
}

/**
 * @brief 处理接收到的UDP数据
 */
void Dispatcher::on_receive(const Endpoint& peer, const char* data, size_t len, uint16_t local_port) {
    if (!data || len == 0) {
        return;
    }

    // 解析包头，获取 packet_id
    std::pair<PacketId, Task::Priority> header = parse_packet_header(data, len);
    PacketId packet_id = header.first;

    HandlerInfo handler_info;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 查找对应的业务回调
        auto it = handlers_.find(packet_id);
        if (it != handlers_.end()) {
            handler_info = it->second;
        } else {
            // 未找到对应的处理函数，使用默认处理函数
            handler_info = default_handler_;
        }
    }

    // 如果没有可用的回调函数，直接返回
    if (!handler_info.callback) {
        std::cerr << "[Dispatcher] No handler for packet_id: " << packet_id 
                  << " from " << peer.to_string() << std::endl;
        return;
    }

    // 构造任务并提交到指定线程组
    // 注意：需要拷贝数据，因为原数据缓冲区可能会被重用
    // 跳过packet_id头部(4字节)，只拷贝业务数据
    std::string payload(data + sizeof(PacketId), len - sizeof(PacketId));
    
    // C++11兼容的lambda（不使用capture initializer）
    BusinessCallback callback = handler_info.callback;
    auto task_callback = [callback, peer, local_port, payload]() mutable {
        muduo::net::Buffer buf;
        buf.append(payload);
        callback(peer, &buf, local_port);
    };

    // 提交到指定的线程组
    if (!thread_pool_->submit(std::move(task_callback), handler_info.group_id, handler_info.priority)) {
        std::cerr << "[Dispatcher] Failed to submit task for packet_id: " << packet_id << std::endl;
    }
}

/**
 * @brief 解析数据包头部
 */
std::pair<PacketId, Task::Priority> Dispatcher::parse_packet_header(const char* data, size_t len) {
    // 当前简化实现：
    // 假设包格式为：[4字节 packet_id（网络字节序）] + [实际数据]
    // 未来可以扩展为更复杂的包头结构，包括优先级字段等
    
    if (len < sizeof(uint32_t)) {
        // 数据不足，无法解析 packet_id
        // 返回默认值
        return {0, Task::NORMAL};
    }

    // 读取 packet_id（网络字节序）
    uint32_t network_packet_id;
    std::memcpy(&network_packet_id, data, sizeof(network_packet_id));
    PacketId packet_id = ntohl(network_packet_id);

    // 未来扩展：可以从包头的其他字段解析优先级
    // 当前版本返回默认优先级，实际优先级由注册时指定
    return {packet_id, Task::NORMAL};
}

}  // namespace udp_framework


