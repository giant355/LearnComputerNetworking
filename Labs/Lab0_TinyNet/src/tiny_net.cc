#include "tiny_net.hh"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace tinynet {

// ---------------- 已完成的地址工具：Lab 0 不要求修改 ----------------

IPv4Address IPv4Address::from_octets(
    const std::uint8_t a,
    const std::uint8_t b,
    const std::uint8_t c,
    const std::uint8_t d
)
{
    // 把 a.b.c.d 四个八位组放入一个 32 位整数。位运算不是本 Lab 重点。
    return IPv4Address {
        (static_cast<std::uint32_t>(a) << 24U)
        | (static_cast<std::uint32_t>(b) << 16U)
        | (static_cast<std::uint32_t>(c) << 8U)
        | static_cast<std::uint32_t>(d)
    };
}

std::string IPv4Address::to_string() const
{
    // 执行相反操作，把 32 位地址重新显示为 a.b.c.d。
    std::ostringstream output;
    output
        << ((value >> 24U) & 0xffU) << '.'
        << ((value >> 16U) & 0xffU) << '.'
        << ((value >> 8U) & 0xffU) << '.'
        << (value & 0xffU);
    return output.str();
}

Router::Router(std::vector<std::size_t> interface_capacities)
    : interface_capacities_(std::move(interface_capacities))
    , queues_(interface_capacities_.size())
{
    // 路由器至少需要一个出口。无效的构造参数属于程序使用错误。
    if (interface_capacities_.empty()) {
        throw std::invalid_argument("a router needs at least one interface");
    }
}

void Router::add_route(Route route)
{
    // Route 引用不存在的接口属于程序配置错误，而不是网络中的正常丢包。
    if (route.interface_index >= queues_.size()) {
        throw std::out_of_range("route refers to an unknown interface");
    }

    routes_.push_back(route);
}

const Route* Router::find_route(const IPv4Address destination) const
{
    // TODO 1：只读取 routes_，寻找 destination 完全相等的现有规则。
    // 找到与找不到都是正常结果；不要创建临时 Route 再返回它的地址。
    for(const Route& r:routes_)
    {
        if (r.destination == destination)
        {
            return &r;
        }
    }
    return nullptr;
}

ReceiveResult Router::receive(Datagram datagram)
{
    // TODO 2：把它看作一条决策流水线：
    // TTL 是否允许转发 → 是否有路由 → 出口队列是否有空间 → 成功入队。
    // 只有成功转发时，TTL 才恰好减少 1，且恰好一个队列增加元素。
    if (datagram.ttl <= 1)
        return ReceiveResult::ttl_expired;
    datagram.ttl--;
    auto tmp = find_route(datagram.destination);
    if (!tmp)
        return ReceiveResult::no_route;
    if (queues_[tmp->interface_index].size() >= interface_capacities_[tmp->interface_index])
        return ReceiveResult::queue_full;

    queues_[tmp->interface_index].push_back(OutboundPacket{tmp->next_hop, datagram });

    return ReceiveResult::forwarded;
    
}

std::optional<OutboundPacket> Router::transmit_one(
    const std::size_t interface_index
)
{
    // TODO 3：无效索引是程序错误；空队列是正常状态；有数据时按 FIFO 取走队首。
    // “取走”意味着返回元素的同时，它也必须从队列中消失。
    if (interface_index >= queues_.size())
    {
        throw std::out_of_range("unknown interface");
    }   
    OutboundPacket packet;
    if (queues_[interface_index].size())
    {
        packet = queues_[interface_index].front();
        queues_[interface_index].pop_front();
        return packet;
    }
    return std::nullopt;
}

std::size_t Router::queued_count(const std::size_t interface_index) const
{
    // 这个观察函数已经完成，可在测试或调试时检查队列是否发生预期变化。
    if (interface_index >= queues_.size()) {
        throw std::out_of_range("unknown interface");
    }

    return queues_[interface_index].size();
}

} // namespace tinynet
