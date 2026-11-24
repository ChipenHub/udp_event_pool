/**
 * @file Task.cc
 * @brief Task类的实现文件
 *
 * 该文件包含Task类的静态成员变量初始化。
 */

#include "Task.h"

namespace udp_framework {

// 静态成员初始化
/** @brief 全局任务序列号计数器 */
uint64_t Task::next_sequence_ = 0;

}  // namespace udp_framework


