# 线程池分组改造总结

## 📋 改造需求

将原来的**一写多读**线程池架构改造为**线程分组 + 单写单读**架构：

1. ✅ 线程池分组：多个线程组，每组有独立编号
2. ✅ 单写单读：每个线程有自己的优先级队列
3. ✅ Round-Robin：组内任务轮询分配
4. ✅ 轻量级锁：每线程独立mutex，最小锁粒度
5. ✅ 构造函数改造：接收数组指定每组线程数
6. ✅ 接口兼容：保留旧接口但限制单组使用

---

## 🏗️ 架构对比

### **旧架构（一写多读）**

```
ThreadPool
    │
    ├── 全局优先级队列（共享）
    │   └── 全局mutex + condition_variable
    │
    └── Worker线程们（共享读取）
        ├── Thread 0
        ├── Thread 1
        ├── Thread 2
        └── Thread 3
```

**问题**：
- 全局锁竞争严重
- 所有线程共享一个队列

---

### **新架构（线程分组 + 单写单读）**

```
ThreadPool
    │
    ├── Group 0 (3 threads) - 普通业务组
    │   ├── Thread 0 (独立队列 + mutex + cv)
    │   ├── Thread 1 (独立队列 + mutex + cv)
    │   └── Thread 2 (独立队列 + mutex + cv)
    │
    ├── Group 1 (2 threads) - 关键业务组
    │   ├── Thread 3 (独立队列 + mutex + cv)
    │   └── Thread 4 (独立队列 + mutex + cv)
    │
    └── Group 2 (2 threads) - 统计业务组
        ├── Thread 5 (独立队列 + mutex + cv)
        └── Thread 6 (独立队列 + mutex + cv)
```

**优势**：
- ✅ 线程隔离：不同组任务完全隔离
- ✅ 最小锁粒度：每线程独立锁
- ✅ Round-Robin：组内轮询分配（无锁atomic）
- ✅ 灵活调度：可按业务特性分组

---

## 🔧 核心改造

### **1. 类型定义（保留灵活性）**

```cpp
/** @brief 线程组ID类型（保留类型灵活性） */
using GroupId = int;

/** @brief 线程ID类型（全局编号） */
using ThreadId = int;
```

### **2. 数据结构**

#### **WorkerThread**
```cpp
struct WorkerThread {
    ThreadId thread_id;                                    // 全局编号
    std::thread thread;                                    // 线程对象
    std::priority_queue<TaskPtr> task_queue;               // 独立优先级队列
    mutable std::mutex queue_mutex;                        // 独立mutex
    std::condition_variable queue_cv;                      // 独立cv
    std::shared_ptr<std::atomic<bool>> running;            // 运行状态（可移动）
};
```

#### **ThreadGroup**
```cpp
struct ThreadGroup {
    GroupId group_id;                                         // 组编号
    std::vector<WorkerThread> workers;                        // 组内线程
    std::shared_ptr<std::atomic<size_t>> round_robin_counter; // 轮询计数（无锁）
};
```

### **3. 构造函数**

#### **新接口（支持分组）**
```cpp
// 示例：Group0有3个线程，Group1有2个，Group2有2个
ThreadPool pool({3, 2, 2});
```

#### **旧接口（兼容单组）**
```cpp
// 创建单个组，包含4个线程
ThreadPool pool(4);
```

---

### **4. 任务提交接口**

#### **新接口（指定线程组）**
```cpp
// 推荐使用：明确指定任务在哪个组执行
pool.submit(callback, group_id, priority);
```

#### **旧接口（兼容单组）**
```cpp
// 仅当只有1个线程组时可用，否则返回false
pool.submit(callback, priority);
```

---

### **5. Dispatcher改造**

#### **新接口（指定线程组）**
```cpp
dispatcher->register_handler(packet_id, callback, group_id, priority);
```

**示例**：
```cpp
// packet_id=1 → Group 0（普通业务组）
dispatcher->register_handler(1, handle_echo, 0, Task::NORMAL);

// packet_id=2 → Group 2（统计业务组）
dispatcher->register_handler(2, handle_stats, 2, Task::LOW);

// packet_id=3 → Group 1（关键业务组）
dispatcher->register_handler(3, handle_critical, 1, Task::CRITICAL);
```

#### **旧接口（兼容单组）**
```cpp
// 仅当只有1个线程组时可用
dispatcher->register_handler(packet_id, callback, priority);
```

---

## 🎯 Round-Robin实现

### **无锁原子计数**

```cpp
// 每个线程组有独立的轮询计数器
std::shared_ptr<std::atomic<size_t>> round_robin_counter;

// 提交任务时无锁选择线程
size_t worker_index = group.round_robin_counter->fetch_add(1, std::memory_order_relaxed) 
                    % group.workers.size();
```

**优势**：
- 完全无锁（atomic操作）
- 公平分配任务
- 高并发性能

---

## 📊 测试结果

### **服务器启动日志**

```
=== UDP Framework Example Server (Multi-Group) ===
[ThreadPool] Started with 3 groups, total 7 threads
  Group 0: 3 threads
  Group 1: 2 threads
  Group 2: 2 threads

[Dispatcher] Registered handler for packet_id: 1, group_id: 0, priority: 2
[Dispatcher] Registered handler for packet_id: 2, group_id: 2, priority: 3
[Dispatcher] Registered handler for packet_id: 3, group_id: 1, priority: 0

Thread Group Info:
  Group 0: 3 threads for normal business
  Group 1: 2 threads for critical business
  Group 2: 2 threads for stats business
```

### **任务执行分离**

- ✅ **packet_id=1 (Echo)** → Group 0 执行
- ✅ **packet_id=2 (Stats)** → Group 2 执行
- ✅ **packet_id=3 (Critical)** → Group 1 执行

---

## 🚀 使用示例

### **完整服务器代码**

```cpp
int main() {
    // 1. 创建分组线程池
    //    Group 0: 3个线程（普通业务）
    //    Group 1: 2个线程（关键业务）
    //    Group 2: 2个线程（统计业务）
    std::vector<size_t> group_configs = {3, 2, 2};
    auto thread_pool = std::make_shared<ThreadPool>(group_configs);
    thread_pool->start();
    
    // 2. 创建Dispatcher
    auto dispatcher = std::make_shared<Dispatcher>(thread_pool);
    
    // 3. 注册业务回调（指定线程组）
    dispatcher->register_handler(1, handle_echo, 0, Task::NORMAL);      // → Group 0
    dispatcher->register_handler(2, handle_stats, 2, Task::LOW);        // → Group 2
    dispatcher->register_handler(3, handle_critical, 1, Task::CRITICAL); // → Group 1
    
    // 4. 创建并启动UdpIO
    auto udp_io = std::make_shared<UdpIO>();
    udp_io->add_listen_port(8001);
    udp_io->set_recv_callback([dispatcher](...) {
        dispatcher->on_receive(...);
    });
    udp_io->start();
    
    // 5. 主循环
    while (udp_io->is_running()) {
        sleep(1);
    }
    
    return 0;
}
```

---

## 📈 性能优化点

### **1. 最小锁粒度**
- 每个线程独立mutex
- 只在任务提交和取出时加锁
- 任务执行在锁外

### **2. Round-Robin无锁**
- 使用atomic计数器
- `memory_order_relaxed`优化
- 无竞争瓶颈

### **3. 线程隔离**
- 不同业务不互相影响
- 关键业务独立线程组
- 避免优先级反转

---

## ⚠️ 注意事项

### **1. 旧接口限制**

```cpp
// ❌ 多组时不能使用旧接口
ThreadPool pool({3, 2});
pool.submit(callback, priority);  // 返回false，必须指定group_id

// ✅ 单组时可以使用旧接口
ThreadPool pool(4);
pool.submit(callback, priority);  // OK
```

### **2. 线程编号规则**

- **全局编号**：0, 1, 2, 3, 4, 5, 6...
- **跨组连续**：
  - Group 0: Thread 0, 1, 2
  - Group 1: Thread 3, 4
  - Group 2: Thread 5, 6

### **3. 类型灵活性**

```cpp
using GroupId = int;    // 可改为 uint8_t, size_t 等
using ThreadId = int;   // 可改为 uint16_t, size_t 等
```

---

## 🎓 最佳实践

### **1. 线程组划分原则**

- **按业务类型分组**：普通/关键/统计
- **按优先级分组**：高/中/低优先级业务
- **按资源需求分组**：CPU密集/IO密集

### **2. 线程数量配置**

```cpp
// 示例：8核CPU
std::vector<size_t> groups = {
    4,  // Group 0: 普通业务（占用一半）
    2,  // Group 1: 关键业务（保证响应）
    2   // Group 2: 统计业务（后台处理）
};
```

### **3. 监控和调优**

```cpp
// 查询待处理任务数
size_t total = pool.pending_tasks();           // 所有组
size_t group0 = pool.pending_tasks(0);         // 指定组

// 查询线程信息
size_t total_threads = pool.thread_count();    // 总线程数
size_t groups = pool.group_count();            // 组数量
size_t group0_threads = pool.group_thread_count(0); // 组内线程数
```

---

## ✅ 改造完成清单

- [x] ThreadPool支持分组
- [x] 每线程独立队列
- [x] Round-Robin轮询
- [x] 轻量级锁
- [x] Dispatcher接口升级
- [x] 旧接口兼容
- [x] 编译通过
- [x] 功能测试通过
- [x] 示例代码更新
- [x] 文档完善

---

## 📝 文件变更列表

### **核心文件**
- `ThreadPool.h` - 完全重构
- `Dispatcher.h` - 接口扩展
- `Dispatcher.cc` - 实现更新
- `example_server.cc` - 示例更新

### **不变文件**
- `Task.h/Task.cc` - 无变化
- `UdpIO.h/UdpIO.cc` - 无变化
- `Buffer.h/Buffer.cc` - 无变化
- `Endpoint.h` - 无变化

---

## 🎉 总结

本次改造成功将UDP网络库的线程池从**一写多读**升级为**线程分组 + 单写单读**架构，实现了：

1. ✅ **更好的隔离性**：不同业务线程组完全隔离
2. ✅ **更高的并发性**：最小锁粒度 + Round-Robin无锁
3. ✅ **更强的灵活性**：支持按业务特性分组调度
4. ✅ **完全兼容性**：保留旧接口用于单组场景

**适用场景**：
- 需要业务隔离的高并发UDP服务器
- 不同优先级业务需要独立资源
- 需要精细化线程资源管理

🚀 **现在就可以工程化应用了！**
