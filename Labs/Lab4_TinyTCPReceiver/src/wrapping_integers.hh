#pragma once

#include <cstdint>

namespace tinytcp {

class Wrap32 {
public:
    /// <summary>用 TCP 首部中实际保存的 32 位值创建一个循环序号。</summary>
    /// <param name="raw_value">线上序号的原始 32 位数值。</param>
    explicit Wrap32(std::uint32_t raw_value)
        : raw_value_(raw_value)
    {}

    /// <summary>取得要写入或刚从 TCP 首部读出的原始 32 位值。</summary>
    [[nodiscard]] std::uint32_t raw_value() const { return raw_value_; }

    bool operator==(const Wrap32&) const = default;

private:
    std::uint32_t raw_value_;
};

/// <summary>把不绕回的内部绝对序号编码为相对于 zero_point 的 32 位线上序号。</summary>
/// <param name="absolute">以 SYN 为绝对序号 0 的 64 位内部位置。</param>
/// <param name="zero_point">本方向的 ISN。</param>
/// <returns>写入 TCP 首部的 32 位循环序号。</returns>
[[nodiscard]] Wrap32 wrap(std::uint64_t absolute, Wrap32 zero_point);

/// <summary>把 32 位线上序号展开为距离 checkpoint 最近的 64 位内部绝对序号。</summary>
/// <param name="value">从 TCP 首部取得的 32 位循环序号。</param>
/// <param name="zero_point">本方向的 ISN。</param>
/// <param name="checkpoint">接收端最近处理位置附近的 64 位参考点。</param>
/// <returns>与 value 同余且距离 checkpoint 最近的内部绝对序号；距离相同时取较小者。</returns>
[[nodiscard]] std::uint64_t unwrap(Wrap32 value, Wrap32 zero_point, std::uint64_t checkpoint);

} // namespace tinytcp
