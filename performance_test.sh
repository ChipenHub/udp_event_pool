#!/bin/bash

# UDP服务器性能测试脚本

echo "=== UDP服务器性能测试脚本 ==="
echo

# 检查可执行文件是否存在
if [ ! -f "build/udp_server" ] || [ ! -f "build/udp_client" ]; then
    echo "错误：可执行文件不存在。请先编译项目："
    echo "  mkdir build && cd build && cmake .. && make"
    exit 1
fi

# 启动服务器
echo "启动UDP服务器..."
cd build
./udp_server 8001 &
SERVER_PID=$!
cd ..

# 等待服务器启动
sleep 2

echo "开始性能测试..."

# 测试1: 并发Echo请求
echo "测试1: 并发Echo请求 (5个客户端同时发送)"
for i in {1..5}; do
    (
        cd build
        for j in {1..10}; do
            ./udp_client 127.0.0.1 8001 > /dev/null 2>&1
        done
    ) &
done

# 等待所有并发请求完成
wait

echo "测试1完成"

# 测试2: 压力测试 - 发送大量数据包
echo "测试2: 压力测试 - 发送大量数据包"
cd build
time (
    for i in {1..100}; do
        ./udp_client 127.0.0.1 8001 > /dev/null 2>&1 &
    done
    wait
)
cd ..

echo "测试2完成"

# 停止服务器
echo "停止服务器..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo "性能测试完成"

# 内存和CPU使用情况（如果有工具的话）
echo
echo "=== 性能建议 ==="
echo "1. 使用 'htop' 或 'top' 监控CPU和内存使用情况"
echo "2. 使用 'netstat -i' 或 'ip -s link' 查看网络统计"
echo "3. 使用 'perf' 进行详细的性能分析"
echo "4. 考虑使用专业的压力测试工具如 'wrk' 或 'ab'"
