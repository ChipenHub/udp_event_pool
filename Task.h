/**
 * @file Task.h
 * @brief 任务封装类 - 封装业务回调函数和优先级信息，支持优先级调度
 *
 * 该文件定义了Task类和相关类型，用于将业务回调函数包装成可调度的任务对象，
 * 支持不同的优先级级别，确保重要任务优先执行。
 */

#ifndef UDP_TASK_H
#define UDP_TASK_H

#include <functional>
#include <memory>

namespace udp_framework {

/**
 * @class Task
 * @brief 任务封装类 - 将业务回调函数包装成可调度的任务对象
 *
 * Task类封装了业务回调函数和优先级信息，支持优先级调度。
 * 使用序列号确保同优先级任务的FIFO（先进先出）顺序。
 */
class Task {
public:
    /** @brief 任务回调函数类型 */
    using TaskCallback = std::function<void()>;

    /**
     * @enum Priority
     * @brief 任务优先级枚举
     *
     * 数字越小优先级越高，用于优先级队列调度。
     */
    enum Priority {
        CRITICAL = 0,   ///< 关键任务 - 最高优先级，必须优先处理
        HIGH = 1,       ///< 高优先级任务
        NORMAL = 2,     ///< 普通优先级任务（默认）
        LOW = 3         ///< 低优先级任务
    };

    /**
     * @brief 构造函数
     * @param callback 任务回调函数
     * @param priority 任务优先级，默认NORMAL
     */
    Task(TaskCallback callback, Priority priority = NORMAL)
        : callback_(std::move(callback)),
          priority_(priority),
          sequence_(next_sequence_++) {
    }

    /**
     * @brief 执行任务
     *
     * 调用封装的回调函数执行实际的业务逻辑。
     */
    void execute() const {
        if (callback_) {
            callback_();
        }
    }

    /**
     * @brief 获取任务优先级
     * @return 任务优先级
     */
    Priority priority() const {
        return priority_;
    }

    /**
     * @brief 获取任务序列号
     * @return 任务序列号，用于同优先级排序
     */
    uint64_t sequence() const {
        return sequence_;
    }

    /**
     * @brief 优先级比较运算符
     * @param other 要比较的另一个Task对象
     * @return true如果当前任务优先级低于other
     *
     * 用于优先级队列排序。优先级高的排在前面，
     * 同优先级的按序列号FIFO顺序。
     */
    bool operator<(const Task& other) const {
        if (priority_ != other.priority_) {
            return priority_ > other.priority_;  // 注意：优先级队列是大顶堆，所以反向比较
        }
        return sequence_ > other.sequence_;  // 同优先级，序列号小的先执行
    }

private:
    /** @brief 任务回调函数 */
    TaskCallback callback_;
    /** @brief 任务优先级 */
    Priority priority_;
    /** @brief 任务序列号，用于同优先级排序 */
    uint64_t sequence_;
    /** @brief 全局序列号生成器 */
    static uint64_t next_sequence_;
};

using TaskPtr = std::shared_ptr<Task>;

}  // namespace udp_framework

#endif  // UDP_TASK_H


