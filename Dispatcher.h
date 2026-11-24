/**
 * @file Dispatcher.h
 * @brief 分发器类 - 负责数据包解析、路由和任务调度
 *
 * 该文件定义了Dispatcher类，作为UDP框架的分发层，负责：
 * - 解析UDP数据包中的packet_id
 * - 根据packet_id路由到对应的业务处理函数
 * - 构造优先级任务并提交到线程池执行
 */

#ifndef UDP_DISPATCHER_H
#define UDP_DISPATCHER_H

#include "Endpoint.h"
#include "Buffer.h"
#include "ThreadPool.h"
#include "Task.h"
#include <functional>
#include <map>
#include <mutex>
#include <memory>

namespace udp_framework {

/** @brief 数据包ID类型 */
using PacketId = uint32_t;

/** @brief 业务回调函数类型 */
using BusinessCallback = std::function<void(const Endpoint&, muduo::net::Buffer*, uint16_t)>;

/**
 * @class Dispatcher
 * @brief 分发器类 - 数据包解析、路由和任务调度
 *
 * Dispatcher类是UDP框架的核心分发层，负责将接收到的UDP数据包
 * 解析并路由到对应的业务处理函数，同时管理任务的优先级调度。
 */
class Dispatcher {
public:
    /**
     * @brief 构造函数
     * @param thread_pool 用于执行任务的线程池
     */
    explicit Dispatcher(std::shared_ptr<ThreadPool> thread_pool)
        : thread_pool_(thread_pool) {
    }

    /** @brief 析构函数 */
    ~Dispatcher() = default;

    // 禁止拷贝和赋值
    Dispatcher(const Dispatcher&) = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;

    /**
     * @brief 注册业务回调处理函数（新接口 - 指定线程组）
     * @param packet_id 数据包ID
     * @param callback 业务处理回调函数
     * @param group_id 目标线程组ID
     * @param priority 任务优先级，默认NORMAL
     *
     * 为指定的packet_id注册业务处理函数，设置其执行线程组和优先级。
     * 任务将在指定的线程组中执行。
     */
    void register_handler(PacketId packet_id,
                         BusinessCallback callback,
                         GroupId group_id,
                         Task::Priority priority = Task::NORMAL);

    /**
     * @brief 注册业务回调处理函数（旧接口 - 兼容单组场景）
     * @param packet_id 数据包ID
     * @param callback 业务处理回调函数
     * @param priority 任务优先级，默认NORMAL
     *
     * 仅当线程池只有一个组时可用，否则注册会失败。
     * 建议使用新接口：register_handler(packet_id, callback, group_id, priority)
     */
    void register_handler(PacketId packet_id,
                         BusinessCallback callback,
                         Task::Priority priority = Task::NORMAL);

    /**
     * @brief 注销业务回调处理函数
     * @param packet_id 要注销的数据包ID
     */
    void unregister_handler(PacketId packet_id);

    /**
     * @brief 处理接收到的UDP数据
     * @param peer 发送端点
     * @param data 接收到的数据指针
     * @param len 数据长度
     * @param local_port 本地接收端口
     *
     * 解析数据包，查找对应的业务回调，构造任务并提交到线程池。
     */
    void on_receive(const Endpoint& peer, const char* data, size_t len, uint16_t local_port);

    /**
     * @brief 设置默认处理回调
     * @param callback 默认的业务处理函数
     * @param group_id 默认处理函数使用的线程组，默认为0
     *
     * 当packet_id未注册时调用此默认处理函数。
     */
    void set_default_handler(BusinessCallback callback, GroupId group_id = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        default_handler_ = HandlerInfo(std::move(callback), group_id, Task::NORMAL);
    }

    /**
     * @brief 获取线程池引用
     * @return 关联的线程池
     */
    std::shared_ptr<ThreadPool> thread_pool() const {
        return thread_pool_;
    }

private:
    /**
     * @brief 解析数据包头部
     * @param data 数据指针
     * @param len 数据长度
     * @return packet_id和优先级的pair
     *
     * 当前简化实现：假设包的前4字节是packet_id（网络字节序）。
     * 未来可扩展为更复杂的包头结构。
     */
    std::pair<PacketId, Task::Priority> parse_packet_header(const char* data, size_t len);

private:
    /**
     * @brief 处理器信息结构
     */
    struct HandlerInfo {
        BusinessCallback callback;      // 业务回调函数
        GroupId group_id;               // 目标线程组
        Task::Priority priority;        // 任务优先级
        
        HandlerInfo() : group_id(-1), priority(Task::NORMAL) {}
        HandlerInfo(BusinessCallback cb, GroupId gid, Task::Priority pri)
            : callback(std::move(cb)), group_id(gid), priority(pri) {}
    };

private:
    /** @brief 关联的线程池，用于执行任务 */
    std::shared_ptr<ThreadPool> thread_pool_;

    /** @brief packet_id到业务回调的映射表 */
    std::map<PacketId, HandlerInfo> handlers_;

    /** @brief 默认业务处理函数信息 */
    HandlerInfo default_handler_;

    /** @brief 保护handlers_和default_handler_的互斥锁 */
    mutable std::mutex mutex_;
};

}  // namespace udp_framework

#endif  // UDP_DISPATCHER_H


