#include "tiny_stream.hh"

#include <algorithm>

namespace tinystream {

ByteStream::ByteStream(const std::size_t capacity)
    : capacity_(capacity)
{}

std::size_t ByteStream::push(const std::string_view data)
{
    // TODO 阶段 1：只接受不超过 available_capacity() 的前缀。
    if (closed_) return 0;

    std::size_t accept = std::min(data.size(),available_capacity());

    buffer_.insert(buffer_.end(),data.begin(),data.begin()+accept);
    bytes_pushed_ += accept;

    return accept;
}

std::string ByteStream::peek(const std::size_t max_len) const
{
    // TODO 阶段 2：复制头部最多 max_len 个字节，不修改状态。
    std::size_t len = std::min(bytes_buffered(), max_len);
    return std::string(buffer_.begin(), buffer_.begin() + len);
}

std::size_t ByteStream::pop(const std::size_t max_len)
{
    // TODO 阶段 2：从头部最多移除 len 个字节，并更新 bytes_popped_。
    std::size_t len = std::min(bytes_buffered(), max_len);
    buffer_.erase(buffer_.begin(), buffer_.begin() + len);
    bytes_popped_ += len;
    return len;
}

void ByteStream::close()
{
    closed_ = true;
}

bool ByteStream::is_closed() const
{
    return closed_;
}

bool ByteStream::is_finished() const
{
    return (is_closed()&&bytes_buffered()==0);
}

std::size_t ByteStream::bytes_buffered() const
{
    return bytes_pushed_ - bytes_popped_;;
}

std::size_t ByteStream::available_capacity() const
{
    return capacity_-bytes_buffered();
}

std::uint64_t ByteStream::bytes_pushed() const
{
    return bytes_pushed_;
}

std::uint64_t ByteStream::bytes_popped() const
{
    return bytes_popped_;
}

} // namespace tinystream
