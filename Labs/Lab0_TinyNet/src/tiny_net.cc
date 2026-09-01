#include "tiny_net.hh"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace tinynet {

IPv4Address IPv4Address::from_octets(
    const std::uint8_t a,
    const std::uint8_t b,
    const std::uint8_t c,
    const std::uint8_t d
)
{
    return IPv4Address {
        (static_cast<std::uint32_t>(a) << 24U)
        | (static_cast<std::uint32_t>(b) << 16U)
        | (static_cast<std::uint32_t>(c) << 8U)
        | static_cast<std::uint32_t>(d)
    };
}

std::string IPv4Address::to_string() const
{
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
    if (interface_capacities_.empty()) {
        throw std::invalid_argument("a router needs at least one interface");
    }
}

void Router::add_route(Route route)
{
    if (route.interface_index >= queues_.size()) {
        throw std::out_of_range("route refers to an unknown interface");
    }

    routes_.push_back(route);
}

const Route* Router::find_route(const IPv4Address destination) const
{
    (void)destination;
    throw std::logic_error("TODO: implement Router::find_route");
}

ReceiveResult Router::receive(Datagram datagram)
{
    (void)datagram;
    throw std::logic_error("TODO: implement Router::receive");
}

std::optional<OutboundPacket> Router::transmit_one(
    const std::size_t interface_index
)
{
    (void)interface_index;
    throw std::logic_error("TODO: implement Router::transmit_one");
}

std::size_t Router::queued_count(const std::size_t interface_index) const
{
    if (interface_index >= queues_.size()) {
        throw std::out_of_range("unknown interface");
    }

    return queues_[interface_index].size();
}

} // namespace tinynet
