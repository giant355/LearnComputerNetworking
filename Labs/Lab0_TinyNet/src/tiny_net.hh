#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace tinynet {

struct IPv4Address {
    std::uint32_t value {};

    static IPv4Address from_octets(
        std::uint8_t a,
        std::uint8_t b,
        std::uint8_t c,
        std::uint8_t d
    );

    [[nodiscard]] std::string to_string() const;

    bool operator==(const IPv4Address&) const = default;
};

struct Datagram {
    IPv4Address source;
    IPv4Address destination;
    std::uint8_t ttl {64};
    std::uint8_t protocol {17};
    std::string payload;
};

struct Route {
    IPv4Address destination;
    IPv4Address next_hop;
    std::size_t interface_index {};
};

struct OutboundPacket {
    IPv4Address next_hop;
    Datagram datagram;
};

enum class ReceiveResult {
    forwarded,
    ttl_expired,
    no_route,
    queue_full,
};

class Router {
public:
    explicit Router(std::vector<std::size_t> interface_capacities);

    void add_route(Route route);

    ReceiveResult receive(Datagram datagram);

    [[nodiscard]] std::optional<OutboundPacket> transmit_one(
        std::size_t interface_index
    );

    [[nodiscard]] std::size_t queued_count(
        std::size_t interface_index
    ) const;

private:
    [[nodiscard]] const Route* find_route(
        IPv4Address destination
    ) const;

    std::vector<Route> routes_;
    std::vector<std::size_t> interface_capacities_;
    std::vector<std::deque<OutboundPacket>> queues_;
};

} // namespace tinynet
