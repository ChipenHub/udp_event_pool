/**
 * @file LockFreeQueue.h
 * @brief 无锁FIFO队列 - 单生产者单消费者（SPSC）
 * 
 * 基于环形缓冲区和atomic实现的无锁队列，专为单写单读场景优化。
 * 特性：
 * - 固定容量（编译期确定）
 * - 无锁操作（使用memory_order优化）
 * - Cache line对齐（避免伪共享）
 * - 高性能FIFO
 */

#ifndef LOCKFREE_QUEUE_H
#define LOCKFREE_QUEUE_H

#include <atomic>
#include <memory>
#include <cstddef>

namespace udp_framework {

/**
 * @class LockFreeQueue
 * @brief 无锁FIFO队列（单生产者单消费者）
 * 
 * @tparam T 元素类型（必须是可移动的）
 * @tparam Capacity 队列容量（必须是2的幂次）
 * 
 * 实现原理：
 * - 环形缓冲区：使用固定大小数组
 * - 读写分离：write_pos_和read_pos_独立，避免false sharing
 * - Memory order：使用relaxed/acquire/release优化性能
 * - 满/空判断：通过比较读写位置
 * 
 * 注意：
 * - 只支持单个生产者和单个消费者
 * - 容量在编译期确定，运行时不可改变
 * - 满了会返回false，不会阻塞
 */
template<typename T, size_t Capacity>
class LockFreeQueue {
    // 静态断言：容量必须是2的幂次（方便取模优化）
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(Capacity > 0, "Capacity must be greater than 0");

public:
    /**
     * @brief 构造函数
     */
    LockFreeQueue() 
        : write_pos_(0)
        , read_pos_(0) {
        // 预先分配所有slot
        for (size_t i = 0; i < Capacity; ++i) {
            buffer_[i] = std::shared_ptr<T>();
        }
    }

    /**
     * @brief 析构函数
     */
    ~LockFreeQueue() = default;

    // 禁止拷贝
    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;
    
    // 允许移动（虽然不推荐在运行时移动）
    LockFreeQueue(LockFreeQueue&& other) noexcept
        : write_pos_(other.write_pos_.load(std::memory_order_relaxed))
        , read_pos_(other.read_pos_.load(std::memory_order_relaxed)) {
        for (size_t i = 0; i < Capacity; ++i) {
            buffer_[i] = std::move(other.buffer_[i]);
        }
    }
    
    LockFreeQueue& operator=(LockFreeQueue&& other) noexcept {
        if (this != &other) {
            write_pos_.store(other.write_pos_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            read_pos_.store(other.read_pos_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            for (size_t i = 0; i < Capacity; ++i) {
                buffer_[i] = std::move(other.buffer_[i]);
            }
        }
        return *this;
    }

    /**
     * @brief 尝试将元素入队（生产者调用）
     * @param item 要入队的元素（右值引用）
     * @return true如果成功入队，false如果队列已满
     * 
     * 注意：只能由单个生产者线程调用
     */
    bool push(T&& item) {
        // 获取当前写位置（relaxed：不需要同步其他内存）
        const size_t write_pos = write_pos_.load(std::memory_order_relaxed);
        // 计算下一个写位置
        const size_t next_write_pos = (write_pos + 1) & (Capacity - 1);
        
        // 检查队列是否已满（acquire：同步读位置）
        if (next_write_pos == read_pos_.load(std::memory_order_acquire)) {
            return false;  // 队列满
        }
        
        // 写入数据
        buffer_[write_pos] = std::make_shared<T>(std::move(item));
        
        // 更新写位置（release：确保数据对消费者可见）
        write_pos_.store(next_write_pos, std::memory_order_release);
        
        return true;
    }

    /**
     * @brief 尝试从队列出队（消费者调用）
     * @param item 用于接收出队元素的引用
     * @return true如果成功出队，false如果队列为空
     * 
     * 注意：只能由单个消费者线程调用
     */
    bool pop(T& item) {
        // 获取当前读位置（relaxed）
        const size_t read_pos = read_pos_.load(std::memory_order_relaxed);
        
        // 检查队列是否为空（acquire：同步写位置）
        if (read_pos == write_pos_.load(std::memory_order_acquire)) {
            return false;  // 队列空
        }
        
        // 读取数据
        auto ptr = buffer_[read_pos];
        if (ptr) {
            item = std::move(*ptr);
            buffer_[read_pos].reset();  // 释放shared_ptr
        } else {
            return false;  // 数据无效
        }
        
        // 更新读位置（release：确保位置更新对生产者可见）
        read_pos_.store((read_pos + 1) & (Capacity - 1), std::memory_order_release);
        
        return true;
    }

    /**
     * @brief 检查队列是否为空
     * @return true如果队列为空
     * 
     * 注意：在并发环境下，返回值可能立即过期
     */
    bool empty() const {
        return read_pos_.load(std::memory_order_acquire) == 
               write_pos_.load(std::memory_order_acquire);
    }

    /**
     * @brief 获取队列当前大小（近似值）
     * @return 当前队列中的元素数量
     * 
     * 注意：在并发环境下只是一个快照，可能不准确
     */
    size_t size() const {
        const size_t write_pos = write_pos_.load(std::memory_order_acquire);
        const size_t read_pos = read_pos_.load(std::memory_order_acquire);
        
        if (write_pos >= read_pos) {
            return write_pos - read_pos;
        } else {
            return Capacity - read_pos + write_pos;
        }
    }

    /**
     * @brief 获取队列容量
     * @return 队列的最大容量
     */
    constexpr size_t capacity() const {
        return Capacity;
    }

private:
    // Cache line大小（避免伪共享）
    static constexpr size_t CACHE_LINE_SIZE = 64;

    // 写位置（对齐到cache line，避免false sharing）
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> write_pos_;
    
    // 读位置（对齐到cache line）
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> read_pos_;
    
    // 环形缓冲区（使用shared_ptr简化内存管理）
    std::shared_ptr<T> buffer_[Capacity];
};

}  // namespace udp_framework

#endif  // LOCKFREE_QUEUE_H
