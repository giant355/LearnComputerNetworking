#include "tiny_stream.hh"

namespace tinystream {

ByteStream::ByteStream(const std::size_t capacity)
    : capacity_(capacity)
{}

std::size_t ByteStream::push(const std::string_view data)
{
    // TODO 阶段 1：只接受不超过 available_capacity() 的前缀。
    static_cast<void>(data);
    return 0;
}

std::string ByteStream::peek(const std::size_t max_len) const
{
    // TODO 阶段 2：复制头部最多 max_len 个字节，不修改状态。
    static_cast<void>(max_len);
    return {};
}

std::size_t ByteStream::pop(const std::size_t len)
{
    // TODO 阶段 2：从头部最多移除 len 个字节，并更新 bytes_popped_。
    static_cast<void>(len);
    return 0;
}

void ByteStream::close()
{
    // TODO 阶段 3：只关闭未来写入，不清空 buffer_。
}

bool ByteStream::is_closed() const
{
    return false;
}

bool ByteStream::is_finished() const
{
    return false;
}

std::size_t ByteStream::bytes_buffered() const
{
    return 0;
}

std::size_t ByteStream::available_capacity() const
{
    return 0;
}

std::uint64_t ByteStream::bytes_pushed() const
{
    return 0;
}

std::uint64_t ByteStream::bytes_popped() const
{
    return 0;
}

} // namespace tinystream
