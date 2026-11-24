# UDP 网络框架设计文档

## 一、架构概述

本框架是一个基于 C++11 的 UDP 网络底层 IO 模块，采用三层架构设计：

```
┌─────────────────────────────────────────────────┐
│              业务层 (Business Layer)             │
│  注册回调、处理业务逻辑、构造响应数据              │
└─────────────────────────────────────────────────┘
                        ↕
┌─────────────────────────────────────────────────┐
│           分发层 (Dispatcher Layer)              │
│  解析包头、路由分发、构造任务、提交线程池           │
└─────────────────────────────────────────────────┘
                        ↕
┌─────────────────────────────────────────────────┐
│              IO 层 (IO Layer)                    │
│  epoll 监听、UDP 收发、多端口管理                 │
└─────────────────────────────────────────────────┘
```

## 二、核心组件

### 2.1 Endpoint（端点封装）

- **职责**：封装 UDP 通信端点（IP + Port）
- **功能**：
  - IP 地址和端口的存储与转换
  - 提供 sockaddr 结构体访问接口
  - 支持比较操作（用于 map/set）

### 2.2 Buffer（数据容器）

- **职责**：统一的数据缓冲区（来自 muduo 库）
- **功能**：
  - 动态扩展的字节数组
  - 读写指针管理
  - 数据追加、读取、消费

### 2.3 Task（任务封装）

- **职责**：封装业务回调和优先级
- **优先级**：
  - CRITICAL (0) - 关键任务
  - HIGH (1) - 高优先级
  - NORMAL (2) - 普通优先级
  - LOW (3) - 低优先级
- **功能**：
  - 支持优先级比较
  - 同优先级按 FIFO 顺序（通过序列号）

### 2.4 ThreadPool（线程池）

- **职责**：多线程并发执行任务
- **特性**：
  - 支持优先级队列调度
  - 可配置线程数量
  - 线程安全的任务提交

### 2.5 UdpIO（IO 层）

- **职责**：网络收发和 epoll 事件驱动
- **功能**：
  - 添加多个监听端口
  - 基于 epoll 的事件循环
  - 非阻塞 UDP 收发
  - 边缘触发模式（ET）

### 2.6 Dispatcher（分发层）

- **职责**：包解析、路由和任务构造
- **功能**：
  - 注册/注销业务回调
  - 解析 packet_id
  - 构造 Task 并提交到线程池
  - 支持默认处理函数

## 三、数据流

### 3.1 接收方向

```
UDP 数据包
    ↓
epoll 事件触发
    ↓
UdpIO::handle_receive()
    ↓
封装 Endpoint + Buffer
    ↓
调用 recv_callback_（即 Dispatcher::on_receive）
    ↓
解析 packet_id
    ↓
查找业务回调
    ↓
构造 Task
    ↓
提交到 ThreadPool
    ↓
Worker 线程执行业务回调
```

### 3.2 发送方向

```
业务代码
    ↓
构造 Buffer
    ↓
调用 UdpIO::send_to()
    ↓
sendto() 系统调用
    ↓
UDP 发送
```

## 四、数据包格式

### 当前版本（简化）

```
┌────────────────┬─────────────────┐
│  4 字节         │   可变长度       │
│  packet_id     │   数据           │
│ (网络字节序)    │                 │
└────────────────┴─────────────────┘
```

### 未来扩展（可演进）

```
┌──────┬──────┬──────┬──────────┬─────────┐
│ 4B   │ 1B   │ 1B   │ 2B       │  可变    │
│ pkt  │ pri  │ flag │ reserved │  数据    │
│ _id  │ ority│ s    │          │         │
└──────┴──────┴──────┴──────────┴─────────┘
```

## 五、优先级支持

### 5.1 当前实现

- 优先级在**注册回调时指定**
- Dispatcher 根据 packet_id 查找对应的优先级
- ThreadPool 使用优先级队列调度

### 5.2 未来扩展

- 支持从包头解析优先级
- IO 层支持优先级分类接收
- 支持优先级动态调整

## 六、使用示例

### 6.1 服务器端

```cpp
// 1. 创建线程池
auto thread_pool = std::make_shared<ThreadPool>(4);
thread_pool->start();

// 2. 创建 Dispatcher
auto dispatcher = std::make_shared<Dispatcher>(thread_pool);

// 3. 注册业务回调
dispatcher->register_handler(1, handle_echo, Task::NORMAL);
dispatcher->register_handler(2, handle_stats, Task::LOW);

// 4. 创建 UdpIO
auto udp_io = std::make_shared<UdpIO>();
udp_io->add_listen_port(8001);
udp_io->add_listen_port(8002);

// 5. 连接 IO 层和 Dispatcher
udp_io->set_recv_callback([dispatcher](const Endpoint& peer, 
                                       Buffer* buffer, 
                                       uint16_t local_port) {
    dispatcher->on_receive(peer, buffer, local_port);
});

// 6. 启动
udp_io->start();
```

### 6.2 业务回调

```cpp
void handle_echo(const Endpoint& peer, Buffer* buffer, uint16_t local_port) {
    // 读取数据
    std::string data = buffer->retrieveAllAsString();
    
    // 处理业务逻辑
    process_data(data);
    
    // 发送响应
    Buffer send_buf;
    send_buf.append(response_data);
    udp_io->send_to(peer, send_buf, local_port);
}
```

## 七、关键设计原则

### 7.1 职责分离

- **IO 层**：只负责网络收发，不做业务处理
- **Dispatcher**：只负责分发和任务构造，不执行业务
- **ThreadPool**：只负责任务执行，不关心业务逻辑

### 7.2 高并发

- 网络接收在独立 IO 线程中进行
- 业务处理在线程池中并发执行
- IO 不会被业务阻塞

### 7.3 可扩展

- 预留优先级扩展能力
- 包格式可演进
- 支持添加可靠传输、重传等特性

## 八、编译和运行

### 8.1 编译

```bash
make
```

### 8.2 运行服务器

```bash
# 使用默认端口 8001, 8002, 8003
./udp_server

# 或指定端口
./udp_server 9001 9002
```

### 8.3 运行客户端

```bash
# 连接到 127.0.0.1:8001
./udp_client

# 或指定服务器
./udp_client 192.168.1.100 9001
```

## 九、性能考虑

### 9.1 零拷贝优化

- 当前版本为了线程安全，在 Dispatcher 中拷贝了 Buffer
- 未来可优化为使用共享指针或引用计数

### 9.2 epoll 边缘触发

- 使用 ET 模式，减少事件触发次数
- 需要循环读取直到 EAGAIN

### 9.3 线程池大小

- 默认使用 CPU 核心数
- 可根据业务特性调整

## 十、未来演进方向

### 10.1 可靠传输

- 添加序列号和 ACK 机制
- 支持重传和超时处理

### 10.2 优先级调度

- IO 层支持优先级队列
- 不同优先级使用不同的 socket

### 10.3 流量控制

- 添加限流和背压机制
- 防止接收队列溢出

### 10.4 监控统计

- 添加性能监控接口
- 统计收发包数量、延迟等指标

