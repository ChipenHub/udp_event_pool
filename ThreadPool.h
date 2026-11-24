/**
 * @file ThreadPool.h
 * @brief 支持分组的无锁线程池 - 每个线程独立FIFO队列，支持线程组隔离
 *
 * 该文件实现了支持线程分组的无锁线程池。
 * 特性：
 * - 线程分组：不同组的线程完全隔离
 * - 单写单读：每个线程独立无锁FIFO队列
 * - Round-Robin：组内任务轮询分配（无锁）
 * - 完全无锁：队列操作不使用mutex
 * - 优先级保留：Priority字段保留但不影响FIFO顺序
 */

#ifndef UDP_THREADPOOL_H
#define UDP_THREADPOOL_H

#include "Task.h"
#include "LockFreeQueue.h"
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <exception>
#include <iostream>

namespace udp_framework {

/** @brief 线程组ID类型（保留类型灵活性） */
using GroupId = int;

/** @brief 线程ID类型（全局编号） */
using ThreadId = int;

/** @brief 每个线程队列的容量（2的幂次，用于高效取模） */
constexpr size_t THREAD_QUEUE_CAPACITY = 2048;

/**
 * @class ThreadPool
 * @brief 支持分组和优先级的线程池类
 *
 * 线程池架构：
 * - 分组管理：多个线程组，每组独立
 * - 单写单读：每个线程独立队列，最小锁粒度
 * - Round-Robin：组内任务轮询分配到线程
 * - 线程隔离：不同组的任务完全隔离
 */
class ThreadPool {
public:
    /**
     * @brief 构造函数（新版 - 支持分组）
     * @param group_thread_counts 每个组的线程数数组
     *        例如：{3, 2, 4} 表示 Group0有3个线程，Group1有2个，Group2有4个
     * 
     * 线程全局编号示例：
     * - Group 0: Thread 0, 1, 2
     * - Group 1: Thread 3, 4
     * - Group 2: Thread 5, 6, 7, 8
     */
    explicit ThreadPool(const std::vector<size_t>& group_thread_counts)
        : running_(false) {
        init_groups(group_thread_counts);
    }

    /**
     * @brief 构造函数（兼容旧版 - 单组）
     * @param thread_count 线程数量
     * 
     * 兼容旧接口，创建单个线程组
     */
    explicit ThreadPool(size_t thread_count = std::thread::hardware_concurrency())
        : running_(false) {
        init_groups({thread_count});
    }

    /**
     * @brief 析构函数
     *
     * 自动调用stop()停止线程池。
     */
    ~ThreadPool() {
        stop();
    }

    // 禁止拷贝和赋值
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    /**
     * @brief 工作线程数据结构
     */
    struct WorkerThread {
        ThreadId thread_id;                                    // 全局线程编号
        std::thread thread;                                    // 线程对象
        LockFreeQueue<TaskPtr, THREAD_QUEUE_CAPACITY> task_queue; // 无锁FIFO队列
        std::shared_ptr<std::atomic<bool>> running;            // 运行状态
        std::shared_ptr<std::atomic<bool>> has_task;           // 是否有任务（用于通知）
        std::mutex wait_mutex;                                 // 等待互斥锁（仅用于条件变量）
        std::condition_variable wait_cv;                       // 等待条件变量
        
        WorkerThread() 
            : thread_id(-1)
            , running(std::make_shared<std::atomic<bool>>(false))
            , has_task(std::make_shared<std::atomic<bool>>(false)) {}
        
        // 禁止拷贝
        WorkerThread(const WorkerThread&) = delete;
        WorkerThread& operator=(const WorkerThread&) = delete;
        
        // 移动构造和赋值
        WorkerThread(WorkerThread&& other) noexcept
            : thread_id(other.thread_id)
            , thread(std::move(other.thread))
            , task_queue(std::move(other.task_queue))
            , running(std::move(other.running))
            , has_task(std::move(other.has_task)) {
            // wait_mutex和wait_cv不可移动，会创建新的
        }
        
        WorkerThread& operator=(WorkerThread&& other) noexcept {
            if (this != &other) {
                thread_id = other.thread_id;
                thread = std::move(other.thread);
                task_queue = std::move(other.task_queue);
                running = std::move(other.running);
                has_task = std::move(other.has_task);
            }
            return *this;
        }
    };
    
    /**
     * @brief 线程组数据结构
     */
    struct ThreadGroup {
        GroupId group_id;                                         // 组编号
        std::vector<WorkerThread> workers;                        // 组内工作线程
        std::shared_ptr<std::atomic<size_t>> round_robin_counter; // Round-Robin计数器
        
        ThreadGroup() : group_id(-1), round_robin_counter(std::make_shared<std::atomic<size_t>>(0)) {}
        
        // 禁止拷贝
        ThreadGroup(const ThreadGroup&) = delete;
        ThreadGroup& operator=(const ThreadGroup&) = delete;
        
        // 移动构造和赋值
        ThreadGroup(ThreadGroup&& other) noexcept
            : group_id(other.group_id),
              workers(std::move(other.workers)),
              round_robin_counter(std::move(other.round_robin_counter)) {}
        
        ThreadGroup& operator=(ThreadGroup&& other) noexcept {
            if (this != &other) {
                group_id = other.group_id;
                workers = std::move(other.workers);
                round_robin_counter = std::move(other.round_robin_counter);
            }
            return *this;
        }
    };

public:

    /**
     * @brief 启动线程池
     *
     * 启动所有组的所有工作线程。
     */
    void start() {
        if (running_.load()) {
            return;
        }
        
        running_.store(true);
        
        // 启动所有组的所有线程
        for (auto& group : groups_) {
            for (auto& worker : group.workers) {
                worker.running->store(true);
                worker.thread = std::thread([this, &worker] {
                    this->worker_thread_func(worker);
                });
            }
        }
        
        std::cout << "[ThreadPool] Started with " << groups_.size() 
                  << " groups, total " << total_thread_count_ << " threads" << std::endl;
    }

    /**
     * @brief 停止线程池
     *
     * 通知所有线程停止，等待它们完成当前任务后退出。
     */
    void stop() {
        if (!running_.load()) {
            return;
        }

        running_.store(false);
        
        // 停止所有组的所有线程
        for (auto& group : groups_) {
            for (auto& worker : group.workers) {
                worker.running->store(false);
                worker.has_task->store(true);  // 唤醒线程
                worker.wait_cv.notify_one();
            }
        }
        
        // 等待所有线程退出
        for (auto& group : groups_) {
            for (auto& worker : group.workers) {
                if (worker.thread.joinable()) {
                    worker.thread.join();
                }
            }
        }
        
        std::cout << "[ThreadPool] Stopped" << std::endl;
    }

    /**
     * @brief 提交任务到指定线程组（新接口 - 推荐）
     * @param callback 要执行的回调函数
     * @param group_id 目标线程组ID
     * @param priority 任务优先级（保留但不影响FIFO顺序），默认NORMAL
     * @return true如果提交成功，false如果队列已满
     *
     * 任务会通过Round-Robin轮询分配到组内的某个线程。
     * 使用无锁FIFO队列，如果队列满则立即返回false。
     */
    bool submit(Task::TaskCallback callback, GroupId group_id, Task::Priority priority = Task::NORMAL) {
        if (!running_.load()) {
            std::cerr << "[ThreadPool] Not running, cannot submit task" << std::endl;
            return false;
        }
        
        // 检查group_id是否有效
        if (group_id < 0 || static_cast<size_t>(group_id) >= groups_.size()) {
            std::cerr << "[ThreadPool] Invalid group_id: " << group_id << std::endl;
            return false;
        }
        
        ThreadGroup& group = groups_[group_id];
        
        // Round-Robin选择组内线程（无锁原子操作）
        size_t worker_index = group.round_robin_counter->fetch_add(1, std::memory_order_relaxed) 
                            % group.workers.size();
        
        WorkerThread& worker = group.workers[worker_index];
        
        // 创建任务（优先级保留但不影响FIFO顺序）
        auto task = std::make_shared<Task>(std::move(callback), priority);
        
        // 无锁入队
        if (!worker.task_queue.push(std::move(task))) {
            // 队列满，返回失败
            std::cerr << "[ThreadPool] Thread " << worker.thread_id 
                      << " queue full, task rejected" << std::endl;
            return false;
        }
        
        // 通知线程有新任务
        worker.has_task->store(true, std::memory_order_release);
        worker.wait_cv.notify_one();
        
        return true;
    }

    /**
     * @brief 提交任务（旧接口 - 兼容单组场景）
     * @param callback 要执行的回调函数
     * @param priority 任务优先级，默认NORMAL
     * @return true如果提交成功
     *
     * 仅当线程池只有一个组时可用，否则直接返回false。
     * 建议使用新接口：submit(callback, group_id, priority)
     */
    bool submit(Task::TaskCallback callback, Task::Priority priority = Task::NORMAL) {
        if (groups_.size() != 1) {
            std::cerr << "[ThreadPool] Multiple groups exist (" << groups_.size() 
                      << "), must specify group_id. Use submit(callback, group_id, priority)" 
                      << std::endl;
            return false;
        }
        
        // 单组时默认提交到group 0
        return submit(std::move(callback), 0, priority);
    }

    /**
     * @brief 获取待处理任务总数（近似值）
     * @return 所有线程队列中等待执行的任务总数
     * 
     * 注意：无锁队列的size是快照，可能不完全准确
     */
    size_t pending_tasks() const {
        size_t total = 0;
        for (const auto& group : groups_) {
            for (const auto& worker : group.workers) {
                total += worker.task_queue.size();
            }
        }
        return total;
    }
    
    /**
     * @brief 获取指定组的待处理任务数（近似值）
     * @param group_id 线程组ID
     * @return 该组所有线程队列中的任务总数
     */
    size_t pending_tasks(GroupId group_id) const {
        if (group_id < 0 || static_cast<size_t>(group_id) >= groups_.size()) {
            return 0;
        }
        
        size_t total = 0;
        const ThreadGroup& group = groups_[group_id];
        for (const auto& worker : group.workers) {
            total += worker.task_queue.size();
        }
        return total;
    }

    /**
     * @brief 获取线程池中的线程总数
     * @return 所有组的线程总数
     */
    size_t thread_count() const {
        return total_thread_count_;
    }
    
    /**
     * @brief 获取线程组数量
     * @return 线程组总数
     */
    size_t group_count() const {
        return groups_.size();
    }
    
    /**
     * @brief 获取指定组的线程数
     * @param group_id 线程组ID
     * @return 该组的线程数量
     */
    size_t group_thread_count(GroupId group_id) const {
        if (group_id < 0 || static_cast<size_t>(group_id) >= groups_.size()) {
            return 0;
        }
        return groups_[group_id].workers.size();
    }

    /**
     * @brief 检查线程池是否正在运行
     * @return true如果线程池正在运行
     */
    bool is_running() const {
        return running_.load();
    }

private:
    /**
     * @brief 初始化线程组
     * @param group_thread_counts 每个组的线程数数组
     */
    void init_groups(const std::vector<size_t>& group_thread_counts) {
        groups_.reserve(group_thread_counts.size());
        
        ThreadId global_thread_id = 0;
        total_thread_count_ = 0;
        
        for (size_t group_idx = 0; group_idx < group_thread_counts.size(); ++group_idx) {
            // 创建新的线程组
            ThreadGroup group;
            group.group_id = static_cast<GroupId>(group_idx);
            group.round_robin_counter->store(0);
            group.workers.reserve(group_thread_counts[group_idx]);
            
            // 创建组内的工作线程
            for (size_t worker_idx = 0; worker_idx < group_thread_counts[group_idx]; ++worker_idx) {
                WorkerThread worker;
                worker.thread_id = global_thread_id++;
                worker.running->store(false);
                group.workers.push_back(std::move(worker));
            }
            
            total_thread_count_ += group_thread_counts[group_idx];
            groups_.push_back(std::move(group));
        }
    }

private:

    /**
     * @brief 工作线程执行函数
     * @param worker 工作线程对象引用
     *
     * 每个线程独立运行此函数：
     * 1. 从无锁队列中尝试获取任务
     * 2. 如果队列为空，等待通知
     * 3. 执行任务（完全无锁）
     */
    void worker_thread_func(WorkerThread& worker) {
        while (worker.running->load(std::memory_order_acquire)) {
            TaskPtr task;
            
            // 尝试从无锁队列中获取任务
            if (worker.task_queue.pop(task)) {
                // 成功获取任务，执行
                try {
                    task->execute();
                } catch (const std::exception& e) {
                    std::cerr << "[ThreadPool] Thread " << worker.thread_id 
                              << " task exception: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "[ThreadPool] Thread " << worker.thread_id 
                              << " task unknown exception" << std::endl;
                }
            } else {
                // 队列为空，等待新任务
                std::unique_lock<std::mutex> lock(worker.wait_mutex);
                
                // 双重检查：防止通知丢失
                if (worker.task_queue.empty() && worker.running->load(std::memory_order_acquire)) {
                    // 重置标志并等待
                    worker.has_task->store(false, std::memory_order_release);
                    
                    // 等待通知或超时（1ms超时避免死锁）
                    worker.wait_cv.wait_for(lock, std::chrono::milliseconds(1), [&worker] {
                        return worker.has_task->load(std::memory_order_acquire) || 
                               !worker.running->load(std::memory_order_acquire);
                    });
                }
            }
        }
    }

private:
    /** @brief 线程池运行状态标志 */
    std::atomic<bool> running_;
    /** @brief 线程总数 */
    size_t total_thread_count_;
    /** @brief 线程组列表 */
    std::vector<ThreadGroup> groups_;
};

}  // namespace udp_framework

#endif  // UDP_THREADPOOL_H


