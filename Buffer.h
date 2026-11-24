/**
 * @file Buffer.h
 * @brief 数据缓冲区类 - 提供动态扩展的字节缓冲区，支持读写操作和数据管理
 *
 * 该文件实现了muduo网络库的Buffer类，用于网络编程中的数据缓冲区管理。
 * 支持动态扩容、读写指针分离、数据追加和消费等操作。
 */

#ifndef MUDUO_NET_BUFFER_H
#define MUDUO_NET_BUFFER_H

#include <algorithm>
#include <vector>
#include <string>
#include <assert.h>
#include <string.h>

namespace muduo
{
namespace net
{

/**
 * @class Buffer
 * @brief 动态缓冲区类 - 仿照Netty的ChannelBuffer设计，支持读写分离
 *
 * Buffer类使用三个索引来管理缓冲区：
 * - prependable bytes: 可用于添加前缀数据的空间
 * - readable bytes: 可读取的有效数据
 * - writable bytes: 可写入的空间
 *
 * @code
 * +-------------------+------------------+------------------+
 * | prependable bytes |  readable bytes  |  writable bytes  |
 * |                   |     (CONTENT)    |                  |
 * +-------------------+------------------+------------------+
 * |                   |                  |                  |
 * 0      <=      readerIndex   <=   writerIndex    <=     size
 * @endcode
 */
class Buffer
{
 public:
  /** @brief 前缀保留字节数，用于添加长度等前缀信息 */
  static const size_t kCheapPrepend = 8;
  /** @brief 初始缓冲区大小 */
  static const size_t kInitialSize = 1024;

  /**
   * @brief 构造函数
   * @param initialSize 初始缓冲区大小（不包括前缀空间）
   */
  explicit Buffer(size_t initialSize = kInitialSize)
    : buffer_(kCheapPrepend + initialSize),
      readerIndex_(kCheapPrepend),
      writerIndex_(kCheapPrepend)
  {
    assert(readableBytes() == 0);
    assert(writableBytes() == initialSize);
    assert(prependableBytes() == kCheapPrepend);
  }

  // implicit copy-ctor, move-ctor, dtor and assignment are fine
  // NOTE: implicit move-ctor is added in g++ 4.6

  /**
   * @brief 交换两个Buffer的内容
   * @param rhs 要交换的Buffer对象
   */
  void swap(Buffer& rhs)
  {
    buffer_.swap(rhs.buffer_);
    std::swap(readerIndex_, rhs.readerIndex_);
    std::swap(writerIndex_, rhs.writerIndex_);
  }

  /**
   * @brief 获取可读字节数
   * @return 可读字节数
   */
  size_t readableBytes() const
  { return writerIndex_ - readerIndex_; }

  /**
   * @brief 获取可写字节数
   * @return 可写字节数
   */
  size_t writableBytes() const
  { return buffer_.size() - writerIndex_; }

  /**
   * @brief 获取前缀可写字节数
   * @return 前缀可写字节数
   */
  size_t prependableBytes() const
  { return readerIndex_; }

  /**
   * @brief 查看可读数据的起始位置
   * @return 可读数据的指针
   */
  const char* peek() const
  { return begin() + readerIndex_; }

  /**
   * @brief 消费指定长度的可读数据
   * @param len 要消费的字节数
   *
   * 移动读指针，标记数据已被消费。
   * 注意返回void是为了防止字符串构造函数中的未定义行为。
   */
  void retrieve(size_t len)
  {
    assert(len <= readableBytes());
    if (len < readableBytes())
    {
      readerIndex_ += len;
    }
    else
    {
      retrieveAll();
    }
  }

  /**
   * @brief 消费所有可读数据，重置缓冲区
   *
   * 将读写指针都重置到初始位置，清空所有数据。
   */
  void retrieveAll()
  {
    readerIndex_ = kCheapPrepend;
    writerIndex_ = kCheapPrepend;
  }

  /**
   * @brief 以字符串形式消费所有可读数据
   * @return 包含所有可读数据的字符串
   */
  std::string retrieveAllAsString()
  {
    return retrieveAsString(readableBytes());
  }

  /**
   * @brief 以字符串形式消费指定长度的数据
   * @param len 要消费的字节数
   * @return 包含指定数据的字符串
   */
  std::string retrieveAsString(size_t len)
  {
    assert(len <= readableBytes());
    std::string result(peek(), len);
    retrieve(len);
    return result;
  }

  /**
   * @brief 追加字符串数据
   * @param str 要追加的字符串
   */
  void append(const std::string& str)
  {
    append(str.data(), str.size());
  }

  /**
   * @brief 追加字符数组数据
   * @param data 数据指针
   * @param len 数据长度
   */
  void append(const char* data, size_t len)
  {
    ensureWritableBytes(len);
    std::copy(data, data+len, beginWrite());
    hasWritten(len);
  }

  /**
   * @brief 追加任意类型数据
   * @param data 数据指针
   * @param len 数据长度（字节）
   */
  void append(const void* data, size_t len)
  {
    append(static_cast<const char*>(data), len);
  }

  /**
   * @brief 确保有足够的可写空间
   * @param len 需要的最小可写字节数
   *
   * 如果当前可写空间不足，会自动扩容。
   */
  void ensureWritableBytes(size_t len)
  {
    if (writableBytes() < len)
    {
      makeSpace(len);
    }
    assert(writableBytes() >= len);
  }

  /**
   * @brief 获取可写位置的指针
   * @return 可写位置的指针
   */
  char* beginWrite()
  { return begin() + writerIndex_; }

  /**
   * @brief 获取可写位置的常量指针
   * @return 可写位置的常量指针
   */
  const char* beginWrite() const
  { return begin() + writerIndex_; }

  /**
   * @brief 标记已写入指定长度的数据
   * @param len 已写入的字节数
   *
   * 移动写指针，标记数据已写入。
   */
  void hasWritten(size_t len)
  {
    assert(len <= writableBytes());
    writerIndex_ += len;
  }

  /**
   * @brief 撤销最近写入的指定长度数据
   * @param len 要撤销的字节数
   *
   * 回退写指针，用于撤销写入操作。
   */
  void unwrite(size_t len)
  {
    assert(len <= readableBytes());
    writerIndex_ -= len;
  }

  /**
   * @brief 获取缓冲区的内部容量
   * @return 缓冲区的容量（字节）
   */
  size_t internalCapacity() const
  {
    return buffer_.capacity();
  }

 private:
  /**
   * @brief 获取缓冲区起始位置的指针
   * @return 缓冲区起始指针
   */
  char* begin()
  { return &*buffer_.begin(); }

  /**
   * @brief 获取缓冲区起始位置的常量指针
   * @return 缓冲区起始常量指针
   */
  const char* begin() const
  { return &*buffer_.begin(); }

  /**
   * @brief 扩容缓冲区，确保有足够空间
   * @param len 需要的最小空间
   *
   * 如果现有空间不够，会选择重新分配内存或移动数据。
   */
  void makeSpace(size_t len)
  {
    if (writableBytes() + prependableBytes() < len + kCheapPrepend)
    {
      // FIXME: move readable data
      buffer_.resize(writerIndex_+len);
    }
    else
    {
      // move readable data to the front, make space inside buffer
      assert(kCheapPrepend < readerIndex_);
      size_t readable = readableBytes();
      std::copy(begin()+readerIndex_,
                begin()+writerIndex_,
                begin()+kCheapPrepend);
      readerIndex_ = kCheapPrepend;
      writerIndex_ = readerIndex_ + readable;
      assert(readable == readableBytes());
    }
  }

 private:
  /** @brief 底层存储缓冲区 */
  std::vector<char> buffer_;
  /** @brief 读指针位置 */
  size_t readerIndex_;
  /** @brief 写指针位置 */
  size_t writerIndex_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_BUFFER_H
