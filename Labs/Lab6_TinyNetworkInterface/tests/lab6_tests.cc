#include "tiny_interface.hh"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using tinylink::ArpMessage;
using tinylink::ArpOpcode;
using tinylink::Datagram;
using tinylink::EthernetFrame;
using tinylink::EtherType;
using tinylink::IPv4Address;
using tinylink::NetworkInterface;
using tinylink::SendResult;
using tinylink::broadcast_mac;

const IPv4Address local_ip = IPv4Address::from_octets(192, 168, 1, 10);
const IPv4Address gateway_ip = IPv4Address::from_octets(192, 168, 1, 1);
const IPv4Address peer_a_ip = IPv4Address::from_octets(192, 168, 1, 77);
const IPv4Address peer_b_ip = IPv4Address::from_octets(192, 168, 1, 88);
const IPv4Address remote_a_ip = IPv4Address::from_octets(203, 0, 113, 20);
const IPv4Address remote_b_ip = IPv4Address::from_octets(198, 51, 100, 8);
const std::string local_mac = "AA:AA:AA:AA:AA:AA";
const std::string gateway_mac = "GG:GG:GG:GG:GG:GG";
const std::string peer_a_mac = "77:77:77:77:77:77";
const std::string peer_b_mac = "88:88:88:88:88:88";

void expect(const bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Datagram dgram(std::string id, const IPv4Address destination)
{
    return Datagram {.id = std::move(id), .source = local_ip, .destination = destination, .ttl = 64, .payload = "payload"};
}

EthernetFrame arp_frame(const ArpOpcode opcode, const IPv4Address sender_ip, std::string sender_mac, const IPv4Address target_ip)
{
    return EthernetFrame {.destination = opcode == ArpOpcode::request ? broadcast_mac : local_mac, .source = sender_mac, .type = EtherType::arp, .datagram = std::nullopt, .arp = ArpMessage {.opcode = opcode, .sender_ip = sender_ip, .sender_mac = std::move(sender_mac), .target_ip = target_ip}, .fcs_ok = true};
}

NetworkInterface make_interface(const std::size_t waiting_capacity = 4)
{
    return NetworkInterface(local_ip, 24, local_mac, gateway_ip, waiting_capacity);
}

void test_route_selects_direct_host_or_gateway()
{
    const auto nic = make_interface();
    expect(nic.next_hop_for(peer_a_ip) == peer_a_ip, "same /24 destination should be its own next hop");
    expect(nic.next_hop_for(remote_a_ip) == gateway_ip, "remote destination should use default gateway");

    const NetworkInterface host_route(local_ip, 32, local_mac, gateway_ip);
    expect(host_route.next_hop_for(peer_a_ip) == gateway_ip, "/32 has no other direct host");

    const NetworkInterface all_direct(local_ip, 0, local_mac, std::nullopt);
    expect(all_direct.next_hop_for(remote_a_ip) == remote_a_ip, "/0 teaching configuration treats every destination as direct");
}

void test_missing_gateway_reports_no_route()
{
    NetworkInterface nic(local_ip, 24, local_mac, std::nullopt);
    expect(nic.send_datagram(dgram("NO-ROUTE", remote_a_ip)) == SendResult::no_route, "remote send without gateway should fail explicitly");
    expect(nic.dropped_count() == 1, "no-route datagram should count as dropped");
    expect(nic.take_frames_to_send().empty(), "no route must not invent an Ethernet frame");
}

void test_direct_target_is_the_arp_key()
{
    auto nic = make_interface();
    expect(nic.send_datagram(dgram("DIRECT", peer_a_ip)) == SendResult::waiting_for_arp, "unknown direct neighbor should wait for ARP");
    expect(nic.waiting_count(peer_a_ip) == 1, "direct datagram should wait under destination IP");
    expect(nic.waiting_count(gateway_ip) == 0, "direct datagram must not wait under gateway");

    const auto frames = nic.take_frames_to_send();
    expect(frames.size() == 1 && frames[0].destination == broadcast_mac, "first miss should broadcast exactly one ARP request");
    expect(frames[0].arp.has_value() && frames[0].arp->target_ip == peer_a_ip, "direct ARP should ask for destination host");
}

void test_remote_destinations_share_gateway_state()
{
    auto nic = make_interface();
    expect(nic.send_datagram(dgram("R1", remote_a_ip)) == SendResult::waiting_for_arp, "first remote datagram should wait");
    expect(nic.send_datagram(dgram("R2", remote_b_ip)) == SendResult::waiting_for_arp, "second remote datagram should wait");
    expect(nic.waiting_count(gateway_ip) == 2, "different remote destinations should share gateway waiting queue");

    const auto frames = nic.take_frames_to_send();
    expect(frames.size() == 1, "quick sends to one gateway need one ARP request");
    expect(frames[0].arp.has_value() && frames[0].arp->target_ip == gateway_ip, "ARP target should be gateway, not final remote host");
}

void test_reply_learns_and_flushes_fifo()
{
    auto nic = make_interface();
    static_cast<void>(nic.send_datagram(dgram("R1", remote_a_ip)));
    static_cast<void>(nic.send_datagram(dgram("R2", remote_b_ip)));
    static_cast<void>(nic.take_frames_to_send());

    expect(!nic.recv_frame(arp_frame(ArpOpcode::reply, gateway_ip, gateway_mac, local_ip)).has_value(), "ARP reply is not IPv4 delivery");
    expect(nic.cached_mac(gateway_ip) == gateway_mac, "ARP reply should create live mapping");
    expect(nic.waiting_count(gateway_ip) == 0, "learned gateway should drain its waiting queue");

    const auto frames = nic.take_frames_to_send();
    expect(frames.size() == 2, "two waiting datagrams should become two frames");
    expect(frames[0].destination == gateway_mac && frames[1].destination == gateway_mac, "both frames should target gateway MAC");
    expect(frames[0].datagram->id == "R1" && frames[1].datagram->id == "R2", "waiting queue should remain FIFO");
    expect(frames[0].datagram->destination == remote_a_ip && frames[1].datagram->destination == remote_b_ip, "ARP must not replace final IP destinations");
}

void test_two_unknown_neighbors_stay_independent()
{
    auto nic = make_interface();
    static_cast<void>(nic.send_datagram(dgram("A", peer_a_ip)));
    static_cast<void>(nic.send_datagram(dgram("B", peer_b_ip)));

    const auto requests = nic.take_frames_to_send();
    expect(requests.size() == 2, "two unknown direct neighbors need independent ARP requests");

    static_cast<void>(nic.recv_frame(arp_frame(ArpOpcode::reply, peer_a_ip, peer_a_mac, local_ip)));
    expect(nic.waiting_count(peer_a_ip) == 0, "A reply should flush A queue");
    expect(nic.waiting_count(peer_b_ip) == 1, "A reply must not flush B queue");

    const auto frames = nic.take_frames_to_send();
    expect(frames.size() == 1 && frames[0].datagram->id == "A", "only A datagram should become ready");
}

void test_receive_filters_before_ipv4_delivery()
{
    auto nic = make_interface();
    const EthernetFrame valid {.destination = local_mac, .source = gateway_mac, .type = EtherType::ipv4, .datagram = dgram("IN", local_ip), .arp = std::nullopt, .fcs_ok = true};
    expect(nic.recv_frame(valid).has_value(), "valid unicast IPv4 frame should be delivered upward");

    EthernetFrame wrong_mac = valid;
    wrong_mac.destination = peer_a_mac;
    expect(!nic.recv_frame(wrong_mac).has_value(), "frame for another MAC should be ignored");

    EthernetFrame broken = valid;
    broken.fcs_ok = false;
    expect(!nic.recv_frame(broken).has_value(), "FCS failure should be ignored");

    EthernetFrame broadcast_ipv4 = valid;
    broadcast_ipv4.destination = broadcast_mac;
    expect(!nic.recv_frame(broadcast_ipv4).has_value(), "this lab does not model IPv4 broadcast delivery");
}

void test_arp_request_learns_and_replies_only_for_self()
{
    auto nic = make_interface();
    static_cast<void>(nic.recv_frame(arp_frame(ArpOpcode::request, peer_a_ip, peer_a_mac, local_ip)));
    expect(nic.cached_mac(peer_a_ip) == peer_a_mac, "ARP request should teach sender mapping");

    auto frames = nic.take_frames_to_send();
    expect(frames.size() == 1 && frames[0].type == EtherType::arp, "owner should send one ARP reply");
    expect(frames[0].destination == peer_a_mac && frames[0].arp->opcode == ArpOpcode::reply, "reply should be unicast to requester");

    static_cast<void>(nic.recv_frame(arp_frame(ArpOpcode::request, peer_b_ip, peer_b_mac, gateway_ip)));
    expect(nic.cached_mac(peer_b_ip) == peer_b_mac, "interface may learn sender even when request asks for someone else");
    expect(nic.take_frames_to_send().empty(), "interface must not answer for an IP it does not own");
}

void test_tick_retries_then_drops_unresolved_neighbor()
{
    auto nic = make_interface();
    static_cast<void>(nic.send_datagram(dgram("WAIT", peer_a_ip)));
    static_cast<void>(nic.take_frames_to_send());

    nic.tick(4'999);
    expect(nic.take_frames_to_send().empty(), "retry must not happen before five seconds");

    nic.tick(1);
    expect(nic.take_frames_to_send().size() == 1, "five seconds should produce second ARP request");

    nic.tick(5'000);
    expect(nic.take_frames_to_send().size() == 1, "ten seconds should produce third ARP request");

    nic.tick(5'000);
    expect(nic.take_frames_to_send().empty(), "after three attempts interface should stop requesting");
    expect(nic.waiting_count(peer_a_ip) == 0, "failed resolution should discard waiting datagrams");
    expect(nic.dropped_count() == 1, "discarded waiting datagram should be counted");
}

void test_waiting_queue_has_per_neighbor_capacity()
{
    auto nic = make_interface(2);
    expect(nic.send_datagram(dgram("Q1", peer_a_ip)) == SendResult::waiting_for_arp, "Q1 should enter queue");
    expect(nic.send_datagram(dgram("Q2", peer_a_ip)) == SendResult::waiting_for_arp, "Q2 should enter queue");
    expect(nic.send_datagram(dgram("Q3", peer_a_ip)) == SendResult::queue_full, "Q3 should be tail-dropped");
    expect(nic.waiting_count(peer_a_ip) == 2, "queue capacity should remain two");
    expect(nic.dropped_count() == 1, "tail drop should be counted");
    expect(nic.take_frames_to_send().size() == 1, "queue pressure must not create duplicate ARP requests");
}

void test_cache_expires_and_next_send_resolves_again()
{
    auto nic = make_interface();
    static_cast<void>(nic.recv_frame(arp_frame(ArpOpcode::reply, gateway_ip, gateway_mac, local_ip)));
    nic.tick(29'999);
    expect(nic.cached_mac(gateway_ip) == gateway_mac, "mapping should remain live before 30 seconds");
    nic.tick(1);
    expect(!nic.cached_mac(gateway_ip).has_value(), "mapping should expire at 30 seconds");

    expect(nic.send_datagram(dgram("AFTER-EXPIRY", remote_a_ip)) == SendResult::waiting_for_arp, "expired mapping should require ARP again");
    const auto frames = nic.take_frames_to_send();
    expect(frames.size() == 1 && frames[0].arp->target_ip == gateway_ip, "new request should ask for gateway again");
}

} // namespace

int main()
{
    const std::pair<const char*, std::function<void()>> tests[] {
        {"Route chooses direct host or gateway", test_route_selects_direct_host_or_gateway},
        {"Missing gateway reports no route", test_missing_gateway_reports_no_route},
        {"Direct target is the ARP key", test_direct_target_is_the_arp_key},
        {"Remote destinations share gateway state", test_remote_destinations_share_gateway_state},
        {"ARP reply learns and flushes FIFO", test_reply_learns_and_flushes_fifo},
        {"Unknown neighbors stay independent", test_two_unknown_neighbors_stay_independent},
        {"Receive filters before IPv4 delivery", test_receive_filters_before_ipv4_delivery},
        {"ARP request learns and replies for self", test_arp_request_learns_and_replies_only_for_self},
        {"Tick retries then drops unresolved neighbor", test_tick_retries_then_drops_unresolved_neighbor},
        {"Waiting queue has per-neighbor capacity", test_waiting_queue_has_per_neighbor_capacity},
        {"Cache expiry requires resolution again", test_cache_expires_and_next_send_resolves_again},
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
