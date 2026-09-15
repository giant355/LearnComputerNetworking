#include "wrapping_integers.hh"
#include <limits>

namespace tinytcp {

Wrap32 wrap(const std::uint64_t absolute, const Wrap32 zero_point)
{
    // TODO 阶段 1：只保留 absolute 的低 32 位，加上 ISN 后让无符号 32 位运算自然回绕。
    return Wrap32{ static_cast<std::uint32_t>(zero_point.raw_value() + absolute) };
}

std::uint64_t unwrap(const Wrap32 value, const Wrap32 zero_point, const std::uint64_t checkpoint)
{
    const std::uint64_t modulus = std::uint64_t{ 1 } << 32;
    const std::uint32_t offset = static_cast<std::uint32_t>(value.raw_value() - zero_point.raw_value());
    const std::uint64_t cycle = checkpoint / modulus;
    const auto distance = [checkpoint](const std::uint64_t candidate) { return candidate > checkpoint ? candidate - checkpoint : checkpoint - candidate; };

    std::uint64_t pos = cycle * modulus + offset;

    if (cycle > 0) {
        const std::uint64_t previous = (cycle - 1) * modulus + offset;
        if (distance(previous) <= distance(pos)) pos = previous;
    }

    if (cycle < std::numeric_limits<std::uint64_t>::max() / modulus) {
        const std::uint64_t next = (cycle + 1) * modulus + offset;
        if (distance(next) < distance(pos)) pos = next;
    }

    return pos;
}

} // namespace tinytcp
