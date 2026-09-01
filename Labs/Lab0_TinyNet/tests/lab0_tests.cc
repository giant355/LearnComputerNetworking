#include "tiny_net.hh"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using tinynet::Datagram;
using tinynet::IPv4Address;
using tinynet::ReceiveResult;
using tinynet::Route;
using tinynet::Router;

IPv4Address ip(
    const std::uint8_t a,
    const std::uint8_t b,
    const std::uint8_t c,
    const std::uint8_t d
)
{
    return IPv4Address::from_octets(a, b, c, d);
}
void expect(const bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Datagram make_datagram(
    const IPv4Address source,
    const IPv4Address destination,
    const std::uint8_t ttl,
    std::string payload
)
{
    return Datagram {
        .source = source,
        .destination = destination,
        .ttl = ttl,
        .protocol = 17,
        .payload = std::move(payload),
    };
}

void test_address_formatting()
{
    expect(
        ip(192, 168, 1, 42).to_string() == "192.168.1.42",
        "IPv4 octets should round-trip through to_string"
    );
}

void test_ttl_expiration()
{
    const auto host_a = ip(10, 0, 0, 1);
    const auto host_b = ip(10, 0, 0, 2);

    Router router({2});
    router.add_route(Route {host_b, host_b, 0});

    const auto result = router.receive(
        make_datagram(host_a, host_b, 1, "expires")
    );

    expect(result == ReceiveResult::ttl_expired, "TTL=1 must expire");
    expect(router.queued_count(0) == 0, "expired packet must not be queued");
}

void test_no_matching_route()
{
    Router router({2});

    const auto result = router.receive(
        make_datagram(ip(10, 0, 0, 1), ip(80, 0, 0, 1), 64, "lost")
    );

    expect(result == ReceiveResult::no_route, "unknown destination needs no_route");
    expect(router.queued_count(0) == 0, "unroutable packet must not be queued");
}

void test_successful_forwarding()
{
    const auto host_a = ip(10, 0, 0, 1);
    const auto host_b = ip(80, 0, 0, 1);
    const auto next_hop = ip(10, 0, 0, 254);

    Router router({2});
    router.add_route(Route {host_b, next_hop, 0});

    const auto result = router.receive(
        make_datagram(host_a, host_b, 7, "JUMP 42")
    );

    expect(result == ReceiveResult::forwarded, "matching route should forward");
    expect(router.queued_count(0) == 1, "forwarded packet should enter queue");

    const auto outbound = router.transmit_one(0);
    expect(outbound.has_value(), "queue should produce one packet");
    expect(outbound->next_hop == next_hop, "next hop must come from route");
    expect(outbound->datagram.source == host_a, "source IP must stay unchanged");
    expect(outbound->datagram.destination == host_b, "destination IP must stay unchanged");
    expect(outbound->datagram.ttl == 6, "successful hop must decrement TTL once");
    expect(outbound->datagram.payload == "JUMP 42", "payload must stay unchanged");
    expect(router.queued_count(0) == 0, "transmit must remove queue front");
}

void test_fifo_and_tail_drop()
{
    const auto host_a = ip(10, 0, 0, 1);
    const auto host_b = ip(80, 0, 0, 1);

    Router router({2});
    router.add_route(Route {host_b, host_b, 0});

    expect(
        router.receive(make_datagram(host_a, host_b, 5, "D1"))
            == ReceiveResult::forwarded,
        "D1 should enter empty queue"
    );
    expect(
        router.receive(make_datagram(host_a, host_b, 5, "D2"))
            == ReceiveResult::forwarded,
        "D2 should fill queue"
    );
    expect(
        router.receive(make_datagram(host_a, host_b, 5, "D3"))
            == ReceiveResult::queue_full,
        "D3 should be tail-dropped"
    );

    expect(router.queued_count(0) == 2, "queue must stay at capacity");

    const auto first = router.transmit_one(0);
    const auto second = router.transmit_one(0);
    const auto empty = router.transmit_one(0);

    expect(first.has_value() && first->datagram.payload == "D1", "D1 must leave first");
    expect(second.has_value() && second->datagram.payload == "D2", "D2 must leave second");
    expect(!empty.has_value(), "empty queue should return nullopt");
}

void test_two_router_path()
{
    const auto host_a = ip(10, 0, 0, 1);
    const auto router_2 = ip(20, 0, 0, 2);
    const auto host_b = ip(80, 0, 0, 1);

    Router r1({2});
    Router r2({2});
    r1.add_route(Route {host_b, router_2, 0});
    r2.add_route(Route {host_b, host_b, 0});

    expect(
        r1.receive(make_datagram(host_a, host_b, 3, "arrives"))
            == ReceiveResult::forwarded,
        "TTL=3 should pass R1"
    );
    auto from_r1 = r1.transmit_one(0);
    expect(from_r1.has_value() && from_r1->datagram.ttl == 2, "R1 should produce TTL=2");

    expect(
        r2.receive(std::move(from_r1->datagram)) == ReceiveResult::forwarded,
        "TTL=2 should pass R2"
    );
    auto to_b = r2.transmit_one(0);
    expect(to_b.has_value() && to_b->datagram.ttl == 1, "B should receive TTL=1");

    expect(
        r1.receive(make_datagram(host_a, host_b, 2, "expires"))
            == ReceiveResult::forwarded,
        "TTL=2 should pass R1"
    );
    from_r1 = r1.transmit_one(0);
    expect(from_r1.has_value() && from_r1->datagram.ttl == 1, "R1 should produce TTL=1");

    expect(
        r2.receive(std::move(from_r1->datagram)) == ReceiveResult::ttl_expired,
        "TTL=1 must expire at R2"
    );
    expect(r2.queued_count(0) == 0, "expired packet must not reach B queue");
}

} // namespace

int main()
{
    const std::pair<const char*, std::function<void()>> tests[] {
        {"IPv4 address formatting", test_address_formatting},
        {"TTL expiration", test_ttl_expiration},
        {"No matching route", test_no_matching_route},
        {"Successful forwarding decrements TTL", test_successful_forwarding},
        {"FIFO queue and tail drop", test_fifo_and_tail_drop},
        {"Two-router path", test_two_router_path},
    };

    std::size_t passed = 0;

    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cout << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }

    std::cout << passed << " test(s) passed\n";
    return passed == std::size(tests) ? 0 : 1;
}
