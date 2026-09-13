#pragma once

#include "tiny_stream.hh"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>

namespace tinyreassembler {

class Reassembler {
public:
    /// <summary>创建一个重组器；输出缓冲区和待重组字节合计最多占用 capacity 个位置。</summary>
    /// <param name="capacity">当前允许保存的未读字节与待重组字节总上限。</param>
    explicit Reassembler(std::size_t capacity);

    /// <summary>接收一个带绝对流位置的片段，裁剪、去重并把连续前缀写入输出 ByteStream。</summary>
    /// <param name="first_index">data 第一个字节在整条流中的逻辑位置。</param>
    /// <param name="data">只在本次调用期间有效的片段内容；实现不能长期保存这个 view。</param>
    /// <param name="is_last_substring">若为 true，原始片段末端就是这个方向的 EOF 位置。</param>
    void insert(std::uint64_t first_index, std::string_view data, bool is_last_substring);

    /// <summary>取得接收端输出字节流，供测试或上层 Reader 查看、消费字节。</summary>
    /// <returns>重组器拥有的 ByteStream。</returns>
    [[nodiscard]] tinystream::ByteStream& output();

    /// <summary>以只读方式取得接收端输出字节流。</summary>
    /// <returns>重组器拥有的只读 ByteStream。</returns>
    [[nodiscard]] const tinystream::ByteStream& output() const;

    /// <summary>返回下一个尚未连续交给 ByteStream 的逻辑位置。</summary>
    [[nodiscard]] std::uint64_t first_unassembled() const;

    /// <summary>返回重组器内部当前暂存的、不重复且尚未交付的字节数。</summary>
    [[nodiscard]] std::size_t bytes_pending() const;

    /// <summary>若已收到最后标记，返回这个方向的 EOF 逻辑位置；否则返回空。</summary>
    [[nodiscard]] std::optional<std::uint64_t> eof_index() const;

private:
    void flush_contiguous();
    void close_if_complete();

    tinystream::ByteStream output_;
    std::map<std::uint64_t, char> pending_;
    std::optional<std::uint64_t> eof_index_;
};

} // namespace tinyreassembler
