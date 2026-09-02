#include "tiny_net.hh"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

// 测试文件的阅读方法：每个 test_* 都按“准备输入 → 执行行为 → 检查结果”组织。
// main 中的测试运行框架可以暂时略过；重点看每个测试保护了什么不变量。

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
    // 缩短测试中的地址写法：ip(10, 0, 0, 1) 表示 10.0.0.1。
    return IPv4Address::from_octets(a, b, c, d);
}
void expect(const bool condition, const std::string& message)
{
    // 条件不成立就让当前测试失败，并显示便于理解的原因。
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
    // 测试辅助函数：制造一份字段明确的数据报，本身不是待实现内容。
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
    // 地址工具的冒烟测试；它通过后，后续测试可以放心使用 ip(...)。
    expect(
        ip(192, 168, 1, 42).to_string() == "192.168.1.42",
        "IPv4 octets should round-trip through to_string"
    );
}

void test_ttl_expiration()
{
    // 准备：存在到 B 的路由，但数据报 TTL=1。
    const auto host_a = ip(10, 0, 0, 1);
    const auto host_b = ip(10, 0, 0, 2);

    Router router({2});
    router.add_route(Route {host_b, host_b, 0});

    // 执行：让路由器接收这份数据报。
    const auto result = router.receive(
        make_datagram(host_a, host_b, 1, "expires")
    );

    // 检查：必须报告 TTL 过期，而且不能污染出口队列。
    expect(result == ReceiveResult::ttl_expired, "TTL=1 must expire");
    expect(router.queued_count(0) == 0, "expired packet must not be queued");
}

void test_no_matching_route()
{
    // 没有匹配路由时必须明确丢弃，不能猜测下一跳。
    Router router({2});

    const auto result = router.receive(
        make_datagram(ip(10, 0, 0, 1), ip(80, 0, 0, 1), 64, "lost")
    );

    expect(result == ReceiveResult::no_route, "unknown destination needs no_route");
    expect(router.queued_count(0) == 0, "unroutable packet must not be queued");
}

void test_successful_forwarding()
{
    // 成功转发测试同时保护：下一跳正确、TTL 恰减 1、端到端字段不变。
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
    // 容量为 2：D1、D2 入队，D3 被队尾丢弃；发送顺序仍是 D1、D2。
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
    // 把两个 Router 串起来，观察同一数据报的 TTL 在每一跳怎样变化。
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
    // 下面只是极小测试运行器：依次调用每个测试并打印 PASS/FAIL。
    // 完成 TODO 时不需要修改这里。
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
