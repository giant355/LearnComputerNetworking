#include "tiny_reassembler.hh"

namespace tinyreassembler {

Reassembler::Reassembler(const std::size_t capacity)
    : output_(capacity)
{}

void Reassembler::insert(const std::uint64_t first_index, const std::string_view data, const bool is_last_substring)
{
    // TODO 阶段 1：若 is_last_substring 为 true，先按原始片段末端记录 EOF。
    // TODO 阶段 2：只保存当前可接受窗口内、尚未交付且不重复的字节。
    // TODO 阶段 3：从 first_unassembled 开始把连续前缀交给 output_。
    // TODO 阶段 4：若连续进度已经到达 EOF，关闭 output_ 的 Writer。
    (void)first_index;
    (void)data;
    (void)is_last_substring;
}

tinystream::ByteStream& Reassembler::output()
{
    return output_;
}

const tinystream::ByteStream& Reassembler::output() const
{
    return output_;
}

std::uint64_t Reassembler::first_unassembled() const
{
    return output_.bytes_pushed();
}

std::size_t Reassembler::bytes_pending() const
{
    return pending_.size();
}

std::optional<std::uint64_t> Reassembler::eof_index() const
{
    return eof_index_;
}

void Reassembler::flush_contiguous()
{
    // TODO 阶段 3：找到从 first_unassembled 开始的连续字符，push 后从 pending_ 删除。
}

void Reassembler::close_if_complete()
{
    // TODO 阶段 4：区分 Writer closed 与 Reader 排空后的 is_finished。
}

} // namespace tinyreassembler
