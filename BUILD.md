# 编译说明

本项目支持两种编译方式：CMake 和 Makefile。推荐使用 CMake。

## 环境要求

- **操作系统**：Linux（使用 epoll）
- **编译器**：支持 C++11 的 g++ 或 clang++
- **CMake**：3.5 或更高版本（使用 CMake 编译时）

## 使用 CMake 编译（推荐）

### 1. 创建构建目录

```bash
mkdir build
cd build
```

### 2. 配置项目

Debug 模式：
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

Release 模式：
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
```

### 3. 编译

```bash
make
```

### 4. 运行

编译产物在 `build/bin` 目录下：

```bash
# 运行服务器
./bin/udp_server 8001 8002 8003

# 运行客户端（在另一个终端）
./bin/udp_client 127.0.0.1 8001
```

### 5. 清理

```bash
# 清理编译产物
make clean

# 完全清理（删除 build 目录）
cd ..
rm -rf build
```

## 使用 Makefile 编译

### 1. 编译

```bash
make
```

### 2. 运行

```bash
# 运行服务器
./udp_server 8001 8002 8003

# 运行客户端
./udp_client 127.0.0.1 8001
```

### 3. 清理

```bash
make clean
```

## 目录结构

编译后的目录结构：

```
udpServer/
├── build/              # CMake 构建目录
│   ├── bin/           # 可执行文件
│   │   ├── udp_server
│   │   └── udp_client
│   └── lib/           # 静态库
│       └── libudp_framework.a
├── *.o                # Makefile 编译的目标文件
├── udp_server         # Makefile 编译的可执行文件
└── udp_client         # Makefile 编译的可执行文件
```

## 常见问题

### Q: 编译时提示找不到 sys/epoll.h

**A**: 本项目使用 Linux 的 epoll API，需要在 Linux 系统上编译。如果在 macOS 上开发，需要在 Linux 虚拟机或容器中编译运行。

### Q: 如何指定编译器

**A**: 使用 CMake 时：

```bash
# 使用 g++
cmake -DCMAKE_CXX_COMPILER=g++ ..

# 使用 clang++
cmake -DCMAKE_CXX_COMPILER=clang++ ..
```

使用 Makefile 时：

```bash
# 使用 clang++
make CXX=clang++
```

### Q: 如何启用更多警告

**A**: 编辑 CMakeLists.txt，在 CMAKE_CXX_FLAGS 中添加：

```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror")
```

## 性能优化

### Release 模式编译

使用 Release 模式编译可以获得更好的性能：

```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

### 链接时优化（LTO）

CMakeLists.txt 中添加：

```cmake
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
```

## 交叉编译

如果需要在 x86_64 机器上编译 ARM 版本，可以使用工具链文件：

```bash
cmake -DCMAKE_TOOLCHAIN_FILE=toolchain-arm.cmake ..
```

