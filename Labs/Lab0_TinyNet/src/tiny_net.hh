#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace tinynet {

// 教学用 IPv4 地址：只保存 32 位地址值，不实现完整的 IPv4 协议。
struct IPv4Address {
    std::uint32_t value {};

    // 下面两个函数已经完成。Lab 0 中可以把它们当作地址输入/显示工具。
    static IPv4Address from_octets(
        std::uint8_t a,
        std::uint8_t b,
        std::uint8_t c,
        std::uint8_t d
    );

    [[nodiscard]] std::string to_string() const;

    bool operator==(const IPv4Address&) const = default;
};

// 一份极简 IP 数据报。Lab 只保留转发过程真正需要观察的字段。
struct Datagram {
    IPv4Address source;       // 端到端来源 IP；普通转发时保持不变。
    IPv4Address destination;  // 最终目标 IP，不是当前链路的下一跳。
    std::uint8_t ttl {64};    // 每经过一台路由器减少 1。
    std::uint8_t protocol {17}; // 17 代表 UDP；本 Lab 不解析上层协议。
    std::string payload;      // 上层载荷，例如 "JUMP 42"。
};

// 一条简化路由规则：本 Lab 只做 destination 完全相等的精确匹配。
struct Route {
    IPv4Address destination;       // 规则适用的最终目标。
    IPv4Address next_hop;          // 当前这一跳应该交给谁。
    std::size_t interface_index {}; // 应进入哪个出口队列。
};

// 已经作出转发决定、正在出口队列中等待发送的数据。
struct OutboundPacket {
    IPv4Address next_hop; // 当前链路使用；下一台路由器收到后会重新决定。
    Datagram datagram;    // 仍然携带端到端的 source 和 destination。
};

// receive 的四种正常结果。它们不是程序异常，而是数据报网络的正常分支。
enum class ReceiveResult {
    forwarded,   // 已进入正确出口队列；不代表最终主机已经收到。
    ttl_expired, // TTL 不允许继续经过当前路由器。
    no_route,    // 路由表中没有匹配规则。
    queue_full,  // 出口队列已满，发生队尾丢弃。
};

class Router {
public:
    // vector 中每个元素对应一个接口的队列容量。
    // 例如 Router({2, 5}) 表示两个接口，容量分别为 2 和 5。
    explicit Router(std::vector<std::size_t> interface_capacities);

    // 添加一条已经给定的路由规则；Lab 0 不研究规则是怎样学习到的。
    void add_route(Route route);

    // 按值接收 datagram，因此函数可以安全修改自己准备转发的那一份。
    ReceiveResult receive(Datagram datagram);

    // 从指定接口发送一个队首元素；空队列用 nullopt 明确表示“没有数据”。
    [[nodiscard]] std::optional<OutboundPacket> transmit_one(
        std::size_t interface_index
    );

    // 只用于观察队列长度，不修改 Router，因此函数末尾带 const。
    [[nodiscard]] std::size_t queued_count(
        std::size_t interface_index
    ) const;

private:
    // 找到时返回指向 routes_ 中现有元素的只读指针；找不到时返回 nullptr。
    [[nodiscard]] const Route* find_route(
        IPv4Address destination
    ) const;

    std::vector<Route> routes_; // 路由表。
    std::vector<std::size_t> interface_capacities_; // 每个出口的最大队列长度。
    std::vector<std::deque<OutboundPacket>> queues_; // 每个接口一个 FIFO 队列。
};

} // namespace tinynet
