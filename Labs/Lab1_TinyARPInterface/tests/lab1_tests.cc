#include "tiny_arp.hh"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using tinyarp::ArpMessage;
using tinyarp::Datagram;
using tinyarp::EthernetFrame;
using tinyarp::FrameType;
using tinyarp::NetworkInterface;
using tinyarp::broadcast_mac;

constexpr const char* local_ip = "10.0.0.2";
constexpr const char* local_mac = "AA:AA:AA:AA:AA:AA";
constexpr const char* gateway_ip = "10.0.0.1";
constexpr const char* gateway_mac = "GG:GG:GG:GG:GG:GG";

void expect(const bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Datagram dgram(std::string id, std::string final_destination = "203.0.113.20")
{
    return Datagram {
        .id = std::move(id),
        .final_destination = std::move(final_destination),
    };
}

EthernetFrame arp_reply(
    std::string sender_ip = gateway_ip,
    std::string sender_mac = gateway_mac,
    std::string target_ip = local_ip
)
{
    return EthernetFrame {
        .destination = local_mac,
        .source = sender_mac,
        .type = FrameType::arp_reply,
        .datagram = std::nullopt,
        .arp = ArpMessage {
            .sender_ip = std::move(sender_ip),
            .sender_mac = std::move(sender_mac),
            .target_ip = std::move(target_ip),
        },
        .fcs_ok = true,
    };
}

void test_cache_miss_queues_and_broadcasts_once()
{
    NetworkInterface nic(local_ip, local_mac);
    nic.send_datagram(dgram("D1"), gateway_ip);
    nic.send_datagram(dgram("D2", "198.51.100.8"), gateway_ip);

    expect(nic.waiting_count(gateway_ip) == 2, "D1 and D2 should wait for the same gateway");

    const auto frames = nic.take_frames_to_send();
    expect(frames.size() == 1, "two quick datagrams need only one ARP request");
    expect(frames[0].type == FrameType::arp_request, "first output should be ARP request");
    expect(frames[0].destination == broadcast_mac, "ARP request should be broadcast");
    expect(frames[0].arp.has_value(), "ARP frame needs ARP message");
    expect(frames[0].arp->sender_ip == local_ip, "request should name local IP");
    expect(frames[0].arp->target_ip == gateway_ip, "request should ask for gateway");
}

void test_reply_learns_and_flushes_waiting_datagrams()
{
    NetworkInterface nic(local_ip, local_mac);
    nic.send_datagram(dgram("D1"), gateway_ip);
    nic.send_datagram(dgram("D2", "198.51.100.8"), gateway_ip);
    static_cast<void>(nic.take_frames_to_send()); // 忽略先前那份 ARP 请求。

    expect(!nic.recv_frame(arp_reply()).has_value(), "ARP reply is not IPv4 delivery");
    expect(nic.cached_mac(gateway_ip) == gateway_mac, "reply should create ARP mapping");
    expect(nic.waiting_count(gateway_ip) == 0, "reply should drain waiting queue");

    const auto frames = nic.take_frames_to_send();
    expect(frames.size() == 2, "D1 and D2 should become two IPv4 frames");
    expect(frames[0].type == FrameType::ipv4 && frames[1].type == FrameType::ipv4, "both outputs are IPv4 frames");
    expect(frames[0].destination == gateway_mac && frames[1].destination == gateway_mac, "both frames target gateway MAC");
    expect(frames[0].datagram->id == "D1" && frames[1].datagram->id == "D2", "waiting order must stay FIFO");
    expect(frames[0].datagram->final_destination == "203.0.113.20", "ARP must not change final destination");
    expect(frames[1].datagram->final_destination == "198.51.100.8", "different final destination stays intact");
}

void test_cache_hit_sends_ipv4_immediately()
{
    NetworkInterface nic(local_ip, local_mac);
    static_cast<void>(nic.recv_frame(arp_reply()));
    static_cast<void>(nic.take_frames_to_send());

    nic.send_datagram(dgram("D3"), gateway_ip);
    expect(nic.waiting_count(gateway_ip) == 0, "cache hit must not create waiting entry");

    const auto frames = nic.take_frames_to_send();
    expect(frames.size() == 1 && frames[0].type == FrameType::ipv4, "cache hit should output IPv4 frame directly");
    expect(frames[0].destination == gateway_mac, "cache hit should use cached MAC");
    expect(frames[0].datagram->id == "D3", "original datagram should be carried");
}

void test_receiving_ipv4_is_not_switch_forwarding()
{
    NetworkInterface nic(local_ip, local_mac);

    const EthernetFrame valid_for_me {
        .destination = local_mac,
        .source = gateway_mac,
        .type = FrameType::ipv4,
        .datagram = dgram("IN"),
        .arp = std::nullopt,
        .fcs_ok = true,
    };
    const auto delivered = nic.recv_frame(valid_for_me);
    expect(delivered.has_value() && delivered->id == "IN", "local interface should deliver IPv4 upward");
    expect(nic.take_frames_to_send().empty(), "receiving IPv4 must not make this interface a switch");

    EthernetFrame wrong_destination = valid_for_me;
    wrong_destination.destination = "ZZ:ZZ:ZZ:ZZ:ZZ:ZZ";
    expect(!nic.recv_frame(wrong_destination).has_value(), "frame for another MAC should be ignored");

    EthernetFrame broken = valid_for_me;
    broken.fcs_ok = false;
    expect(!nic.recv_frame(broken).has_value(), "FCS failure should be ignored");
}

void test_arp_request_learns_and_replies_for_self()
{
    NetworkInterface nic(local_ip, local_mac);
    const EthernetFrame request {
        .destination = broadcast_mac,
        .source = gateway_mac,
        .type = FrameType::arp_request,
        .datagram = std::nullopt,
        .arp = ArpMessage {
            .sender_ip = gateway_ip,
            .sender_mac = gateway_mac,
            .target_ip = local_ip,
        },
        .fcs_ok = true,
    };

    static_cast<void>(nic.recv_frame(request));
    expect(nic.cached_mac(gateway_ip) == gateway_mac, "ARP request should teach sender mapping");

    const auto frames = nic.take_frames_to_send();
    expect(frames.size() == 1 && frames[0].type == FrameType::arp_reply, "owner of target IP should reply");
    expect(frames[0].destination == gateway_mac, "reply should be unicast to requester");
    expect(frames[0].arp->sender_ip == local_ip && frames[0].arp->sender_mac == local_mac, "reply should state local identity");
    expect(frames[0].arp->target_ip == gateway_ip, "reply should address requester IP");
}

void test_cache_expires_then_new_data_requests_again()
{
    NetworkInterface nic(local_ip, local_mac);
    static_cast<void>(nic.recv_frame(arp_reply()));
    nic.tick(30'000);
    expect(!nic.cached_mac(gateway_ip).has_value(), "cache entry should expire at 30 seconds");

    nic.send_datagram(dgram("D4"), gateway_ip);
    expect(nic.waiting_count(gateway_ip) == 1, "new data after expiry should wait again");
    const auto frames = nic.take_frames_to_send();
    expect(frames.size() == 1 && frames[0].type == FrameType::arp_request, "new data after expiry should request again");
}

} // namespace

int main()
{
    const std::pair<const char*, std::function<void()>> tests[] {
        {"Cache miss queues and broadcasts once", test_cache_miss_queues_and_broadcasts_once},
        {"ARP reply learns and flushes queue", test_reply_learns_and_flushes_waiting_datagrams},
        {"Cache hit sends IPv4 immediately", test_cache_hit_sends_ipv4_immediately},
        {"IPv4 reception is not switch forwarding", test_receiving_ipv4_is_not_switch_forwarding},
        {"ARP request learns and replies", test_arp_request_learns_and_replies_for_self},
        {"Cache expiry requires new request", test_cache_expires_then_new_data_requests_again},
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
