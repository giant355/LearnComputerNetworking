#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

namespace tinystream {

class ByteStream {
public:
    /// <summary>创建一个最多保存 capacity 个未读字节的有限容量字节流。</summary>
    /// <param name="capacity">当前缓冲区允许同时保存的未读字节上限。</param>
    explicit ByteStream(std::size_t capacity);

    /// <summary>尽量追加 data，返回本次实际接受的字节数；空间不足时只接受前缀，关闭后返回 0。</summary>
    /// <param name="data">调用者暂时借用的输入字节；实现不得保存这个 view。</param>
    /// <returns>实际复制进内部缓冲区的字节数。</returns>
    std::size_t push(std::string_view data);

    /// <summary>查看流头部最多 max_len 个字节，不改变缓冲区或累计计数。</summary>
    /// <param name="max_len">最多查看的字节数。</param>
    /// <returns>头部字节的副本；没有可读字节时返回空字符串。</returns>
    [[nodiscard]] std::string peek(std::size_t max_len) const;

    /// <summary>从流头部最多消耗 len 个字节，返回实际消耗量。</summary>
    /// <param name="len">请求消耗的字节数；超过当前缓冲量时只能消耗现有字节。</param>
    /// <returns>实际从缓冲区移除的字节数。</returns>
    std::size_t pop(std::size_t len);

    /// <summary>关闭写入端；保留已经接受但尚未读取的字节。</summary>
    void close();

    /// <summary>查询 Writer 是否已经关闭。</summary>
    [[nodiscard]] bool is_closed() const;

    /// <summary>查询 Writer 已关闭且缓冲区已经排空的结束状态。</summary>
    [[nodiscard]] bool is_finished() const;

    /// <summary>返回当前未读字节数。</summary>
    [[nodiscard]] std::size_t bytes_buffered() const;

    /// <summary>返回当前还可以接受的字节数。</summary>
    [[nodiscard]] std::size_t available_capacity() const;

    /// <summary>返回从创建以来累计被流接受的字节数。</summary>
    [[nodiscard]] std::uint64_t bytes_pushed() const;

    /// <summary>返回从创建以来累计被 Reader 消耗的字节数。</summary>
    [[nodiscard]] std::uint64_t bytes_popped() const;

private:
    std::size_t capacity_;
    std::deque<char> buffer_;
    std::uint64_t bytes_pushed_ { 0 };
    std::uint64_t bytes_popped_ { 0 };
    bool closed_ { false };
};

} // namespace tinystream
