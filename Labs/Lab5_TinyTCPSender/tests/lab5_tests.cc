#include "tiny_tcp_sender.hh"

#include <cstdint>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using tinytcp::TcpReceiverMessage;
using tinytcp::TcpSender;
using tinytcp::TcpSenderSegment;
using tinytcp::Wrap32;

void expect(const bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

std::vector<TcpSenderSegment> take(TcpSender& sender)
{
    return sender.take_segments_to_send();
}

void test_sequence_length_counts_flags()
{
    expect(TcpSenderSegment { Wrap32 { 0 }, false, false, "ABC" }.sequence_length() == 3, "payload should occupy one sequence position per byte");
    expect(TcpSenderSegment { Wrap32 { 0 }, true, true, "ABC" }.sequence_length() == 5, "SYN and FIN should each occupy one extra position");
}

void test_initial_push_sends_only_syn()
{
    TcpSender sender(16, Wrap32 { 1000 }, 1000);
    sender.stream().push("ABC");
    sender.push();
    const auto sent = take(sender);
    expect(sent.size() == 1 && sent[0].syn && sent[0].payload.empty(), "the initial one-position window should contain only SYN");
    expect(sent[0].seqno == Wrap32 { 1000 }, "SYN should use the ISN");
    expect(sender.sequence_numbers_in_flight() == 1 && sender.next_seqno_abs() == 1, "SYN should advance the internal position by one");
}

void test_ack_opens_window_and_respects_mss()
{
    TcpSender sender(16, Wrap32 { 1000 }, 1000, 3);
    sender.stream().push("ABCD");
    sender.push();
    take(sender);
    sender.receive(TcpReceiverMessage { Wrap32 { 1001 }, 4 });
    const auto sent = take(sender);
    expect(sent.size() == 2, "four available positions with MSS three should make two data segments");
    expect(sent[0].seqno == Wrap32 { 1001 } && sent[0].payload == "ABC", "the first data segment should carry ABC");
    expect(sent[1].seqno == Wrap32 { 1004 } && sent[1].payload == "D", "the second data segment should use the final window position");
    expect(sender.sequence_numbers_in_flight() == 4 && sender.outstanding_count() == 2, "ABC and D should both remain outstanding");
}

void test_cumulative_ack_removes_only_covered_segments()
{
    TcpSender sender(16, Wrap32 { 1000 }, 1000, 3);
    sender.stream().push("ABCD");
    sender.push();
    take(sender);
    sender.receive(TcpReceiverMessage { Wrap32 { 1001 }, 4 });
    take(sender);
    sender.receive(TcpReceiverMessage { Wrap32 { 1004 }, 4 });
    expect(sender.sequence_numbers_in_flight() == 1, "ACK 1004 should leave only D in flight");
    expect(sender.outstanding_count() == 1, "ABC should be erased while D remains outstanding");
}

void test_duplicate_ack_can_expand_window_without_resetting_history()
{
    TcpSender sender(16, Wrap32 { 1000 }, 1000, 3);
    sender.stream().push("ABCD");
    sender.push();
    take(sender);
    sender.receive(TcpReceiverMessage { Wrap32 { 1001 }, 2 });
    auto sent = take(sender);
    expect(sent.size() == 1 && sent[0].payload == "AB", "window two should initially allow AB");
    sender.receive(TcpReceiverMessage { Wrap32 { 1001 }, 4 });
    sent = take(sender);
    expect(sent.size() == 1 && sent[0].payload == "CD", "the same ACK with a larger window should allow CD");
}

void test_timeout_retransmits_oldest_and_backs_off()
{
    TcpSender sender(8, Wrap32 { 5000 }, 1000);
    sender.push();
    take(sender);
    sender.tick(999);
    expect(take(sender).empty(), "the sender must not retransmit before RTO");
    sender.tick(1);
    auto sent = take(sender);
    expect(sent.size() == 1 && sent[0].syn, "the first timeout should retransmit SYN");
    expect(sender.sequence_numbers_in_flight() == 1 && sender.consecutive_retransmissions() == 1 && sender.current_rto_ms() == 2000, "retransmission should not add flight size and should double nonzero-window RTO");
    sender.tick(2000);
    sent = take(sender);
    expect(sent.size() == 1 && sent[0].syn, "the second timeout should still retransmit the oldest SYN");
    expect(sender.consecutive_retransmissions() == 2 && sender.current_rto_ms() == 4000, "a second timeout should double RTO again");
}

void test_new_ack_resets_timer_and_tracks_new_oldest()
{
    TcpSender sender(16, Wrap32 { 1000 }, 1000, 3);
    sender.stream().push("ABCD");
    sender.push();
    take(sender);
    sender.receive(TcpReceiverMessage { Wrap32 { 1001 }, 4 });
    take(sender);
    sender.tick(1000);
    take(sender);
    sender.receive(TcpReceiverMessage { Wrap32 { 1004 }, 4 });
    expect(sender.consecutive_retransmissions() == 0 && sender.current_rto_ms() == 1000, "a progressing ACK should restore retransmission state");
    sender.tick(999);
    expect(take(sender).empty(), "the new oldest segment should receive a fresh full RTO");
    sender.tick(1);
    const auto sent = take(sender);
    expect(sent.size() == 1 && sent[0].payload == "D", "after the fresh RTO only D should be retransmitted");
}

void test_zero_window_probe_does_not_back_off()
{
    TcpSender sender(8, Wrap32 { 7000 }, 1000);
    sender.stream().push("Z");
    sender.push();
    take(sender);
    sender.receive(TcpReceiverMessage { Wrap32 { 7001 }, 0 });
    auto sent = take(sender);
    expect(sent.size() == 1 && sent[0].payload == "Z", "a zero window should permit one probe position");
    sender.tick(1000);
    sent = take(sender);
    expect(sent.size() == 1 && sent[0].payload == "Z", "an unacknowledged probe should be retransmitted");
    expect(sender.consecutive_retransmissions() == 0 && sender.current_rto_ms() == 1000, "zero-window probing should not back off or count as congestion retransmission");
}

void test_fin_waits_for_space_and_occupies_one_position()
{
    TcpSender sender(8, Wrap32 { 1000 }, 1000, 3);
    sender.stream().push("XYZ");
    sender.stream().close();
    sender.push();
    take(sender);
    sender.receive(TcpReceiverMessage { Wrap32 { 1001 }, 2 });
    auto sent = take(sender);
    expect(sent.size() == 1 && sent[0].payload == "XY" && !sent[0].fin, "a two-position window cannot also fit FIN after XY");
    sender.receive(TcpReceiverMessage { Wrap32 { 1003 }, 2 });
    sent = take(sender);
    expect(sent.size() == 1 && sent[0].payload == "Z" && sent[0].fin, "after ACK, Z and FIN should share the two positions");
    expect(sent[0].sequence_length() == 2 && sender.sequence_numbers_in_flight() == 2, "Z plus FIN should occupy two sequence positions");
}

void test_sequence_numbers_wrap_on_wire()
{
    TcpSender sender(8, Wrap32 { 0xffff'fffeU }, 1000, 3);
    sender.stream().push("AB");
    sender.push();
    take(sender);
    sender.receive(TcpReceiverMessage { Wrap32 { 0xffff'ffffU }, 2 });
    const auto sent = take(sender);
    expect(sent.size() == 1 && sent[0].seqno == Wrap32 { 0xffff'ffffU } && sent[0].payload == "AB", "payload should start at the wrapped ISN plus one");
    expect(sender.next_seqno_abs() == 3, "internal sequence positions should keep increasing across wire wrap");
}

void test_impossible_ack_is_ignored()
{
    TcpSender sender(8, Wrap32 { 1000 }, 1000);
    sender.stream().push("A");
    sender.push();
    take(sender);
    sender.receive(TcpReceiverMessage { Wrap32 { 1005 }, 10 });
    expect(sender.sequence_numbers_in_flight() == 1 && sender.outstanding_count() == 1, "an ACK beyond next_seqno_abs must not confirm unsent positions");
    sender.push();
    expect(take(sender).empty(), "an invalid ACK must not smuggle in a larger advertised window");
}

} // namespace

int main()
{
    const std::pair<const char*, std::function<void()>> tests[] {
        { "sequence length", test_sequence_length_counts_flags },
        { "initial SYN", test_initial_push_sends_only_syn },
        { "window fill and MSS", test_ack_opens_window_and_respects_mss },
        { "cumulative ACK cleanup", test_cumulative_ack_removes_only_covered_segments },
        { "duplicate ACK window update", test_duplicate_ack_can_expand_window_without_resetting_history },
        { "timeout and backoff", test_timeout_retransmits_oldest_and_backs_off },
        { "new ACK resets timer", test_new_ack_resets_timer_and_tracks_new_oldest },
        { "zero-window probe", test_zero_window_probe_does_not_back_off },
        { "FIN waits for space", test_fin_waits_for_space_and_occupies_one_position },
        { "wire sequence wrap", test_sequence_numbers_wrap_on_wire },
        { "invalid ACK", test_impossible_ack_is_ignored },
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
