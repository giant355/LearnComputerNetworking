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


    if (is_last_substring)
        eof_index_ = first_index + data.size();

    std::uint64_t fragment_end = first_index + data.size();
    std::uint64_t left = first_unassembled();
    std::uint64_t right = left + output_.available_capacity();

    std::uint64_t accepted_begin = std::max(first_index, left);//逻辑位置
    std::uint64_t accepted_end = std::min(fragment_end, right);

    if (accepted_begin >= accepted_end) {
        close_if_complete();
        return;
    }

    for (std::uint64_t position = accepted_begin; position < accepted_end; ++position) {
        const std::size_t data_index = static_cast<std::size_t>(position - first_index);
        pending_.try_emplace(position, data[data_index]);
    }

    flush_contiguous();

    close_if_complete();
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

//把 pending_ 中从 first_unassembled() 开始连续存在的字节，按顺序转移到 output_。
void Reassembler::flush_contiguous()
{
    // TODO 阶段 3：找到从 first_unassembled 开始的连续字符，push 后从 pending_ 删除。
    auto it = pending_.find(first_unassembled());

    while (true)
    {
        auto it = pending_.find(first_unassembled());
        if (it == pending_.end()) break;

        const std::string byte(1, it->second);
        if (output_.push(byte) != 1) break;//push successful -> bytes_pushed++ -> first_unassembled++

        pending_.erase(it);
    }
}

void Reassembler::close_if_complete()
{
    // TODO 阶段 4：区分 Writer closed 与 Reader 排空后的 is_finished。
    if (eof_index_.has_value() && first_unassembled() == *eof_index_) {
        output_.close();
    }
}

} // namespace tinyreassembler
