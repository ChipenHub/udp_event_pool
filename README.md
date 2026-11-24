# UDP 网络框架

一个基于 C++11 的高性能 UDP 网络底层 IO 模块框架。

## 特性

- ✅ **epoll 事件驱动**：高效的 IO 多路复用
- ✅ **多端口监听**：单进程支持多个 UDP 端口
- ✅ **优先级调度**：支持任务优先级队列
- ✅ **职责分离**：清晰的三层架构（IO / Dispatcher / Business）
- ✅ **线程池**：多 worker 并发处理业务
- ✅ **易于扩展**：预留可靠传输、优先级等演进空间

## 快速开始

### 环境要求

- Linux 操作系统（使用 epoll）
- C++11 编译器（g++ 或 clang++）
- CMake 3.5+ （推荐）或 Make

### 编译（使用 CMake）

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

### 编译（使用 Makefile）

```bash
make
```

### 运行示例服务器

```bash
# CMake 编译
./build/bin/udp_server 8001 8002 8003

# Makefile 编译
./udp_server 8001 8002 8003
```

### 运行示例客户端

```bash
# CMake 编译
./build/bin/udp_client 127.0.0.1 8001

# Makefile 编译
./udp_client 127.0.0.1 8001
```

详细编译说明请参考 [BUILD.md](BUILD.md)。

## 架构

```
┌─────────────┐
│  Business   │  业务层：注册回调、处理逻辑
└──────┬──────┘
       │
┌──────▼──────┐
│ Dispatcher  │  分发层：解析、路由、构造任务
└──────┬──────┘
       │
┌──────▼──────┐
│   UdpIO     │  IO 层：epoll + UDP 收发
└─────────────┘
```

## 核心组件

| 组件 | 职责 |
|------|------|
| **Endpoint** | UDP 端点封装（IP + Port）|
| **Buffer** | 数据容器（muduo::net::Buffer）|
| **Task** | 任务封装（回调 + 优先级）|
| **ThreadPool** | 线程池（优先级队列调度）|
| **UdpIO** | IO 层（epoll + 多端口监听）|
| **Dispatcher** | 分发层（解析 + 路由 + 任务构造）|

## 使用示例

```cpp
// 1. 创建线程池
auto thread_pool = std::make_shared<ThreadPool>(4);
thread_pool->start();

// 2. 创建 Dispatcher 并注册业务回调
auto dispatcher = std::make_shared<Dispatcher>(thread_pool);
dispatcher->register_handler(1, handle_echo, Task::NORMAL);

// 3. 创建 UdpIO 并添加监听端口
auto udp_io = std::make_shared<UdpIO>();
udp_io->add_listen_port(8001);

// 4. 连接 IO 层和 Dispatcher
udp_io->set_recv_callback([dispatcher](auto& peer, auto* buf, auto port) {
    dispatcher->on_receive(peer, buf, port);
});

// 5. 启动
udp_io->start();
```

## 数据包格式

当前版本使用简化格式：

```
┌────────────────┬─────────────────┐
│  4 字节         │   可变长度       │
│  packet_id     │   数据           │
│ (网络字节序)    │                 │
└────────────────┴─────────────────┘
```

- `packet_id`：用于路由到对应的业务回调
- 未来可扩展为包含优先级、标志位等字段

## 优先级

支持 4 个优先级级别：

- **CRITICAL** (0)：关键任务，最高优先级
- **HIGH** (1)：高优先级
- **NORMAL** (2)：普通优先级（默认）
- **LOW** (3)：低优先级

同优先级任务按 FIFO 顺序执行。

## 文件结构

```
udpServer/
├── Buffer.h / Buffer.cc          # 数据容器（muduo）
├── Endpoint.h                     # UDP 端点封装
├── Task.h / Task.cc              # 任务封装
├── ThreadPool.h                   # 线程池
├── UdpIO.h / UdpIO.cc            # IO 层
├── Dispatcher.h / Dispatcher.cc  # 分发层
├── example_server.cc              # 示例服务器
├── example_client.cc              # 示例客户端
├── CMakeLists.txt                 # CMake 构建文件
├── Makefile                       # Make 构建文件
├── README.md                      # 本文件
├── DESIGN.md                      # 详细设计文档
└── BUILD.md                       # 编译说明
```

## 设计原则

1. **职责分离**：IO、分发、业务三层职责清晰
2. **高并发**：IO 不阻塞，业务并发执行
3. **可扩展**：预留可靠传输、优先级等演进空间

## 性能特性

- **epoll 边缘触发**：减少事件触发次数
- **非阻塞 IO**：避免阻塞
- **多线程并发**：充分利用多核 CPU
- **优先级队列**：重要任务优先处理

## 测试

启动服务器后，客户端会发送不同类型的测试包：

- **packet_id=1**：Echo 服务（会回传数据）
- **packet_id=2**：统计服务（只记录）
- **packet_id=3**：关键服务（高优先级）
- **packet_id=999**：未知类型（触发默认处理）

## 许可证

本项目基于 muduo 网络库的 Buffer 实现，遵循 BSD 许可证。

## 详细文档

更多设计细节请参考 [DESIGN.md](DESIGN.md)。

