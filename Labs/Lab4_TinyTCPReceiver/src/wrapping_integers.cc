#include "wrapping_integers.hh"

namespace tinytcp {

Wrap32 wrap(const std::uint64_t absolute, const Wrap32 zero_point)
{
    // TODO 阶段 1：只保留 absolute 的低 32 位，加上 ISN 后让无符号 32 位运算自然回绕。
    (void)absolute;
    return zero_point;
}

std::uint64_t unwrap(const Wrap32 value, const Wrap32 zero_point, const std::uint64_t checkpoint)
{
    // TODO 阶段 2：先求本圈内相对偏移，再在 checkpoint 前后圈中选择距离最近的 64 位候选。
    (void)value;
    (void)zero_point;
    (void)checkpoint;
    return 0;
}

} // namespace tinytcp
