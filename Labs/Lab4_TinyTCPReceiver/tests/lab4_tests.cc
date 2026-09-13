#include "tiny_tcp_receiver.hh"
#include "wrapping_integers.hh"

#include <cstdint>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using tinytcp::TcpReceiver;
using tinytcp::TcpSegment;
using tinytcp::Wrap32;
using tinytcp::unwrap;
using tinytcp::wrap;

constexpr std::uint64_t modulus = std::uint64_t { 1 } << 32;

void expect(const bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void test_wrap_basic_and_cycle()
{
    const Wrap32 isn { 1000 };
    expect(wrap(0, isn) == Wrap32 { 1000 }, "absolute zero should wrap to the ISN");
    expect(wrap(6, isn) == Wrap32 { 1006 }, "a small absolute offset should be added to the ISN");
    expect(wrap(modulus + 6, isn) == Wrap32 { 1006 }, "one full cycle should produce the same 32-bit value");
    expect(wrap(modulus - 1, Wrap32 { 2 }) == Wrap32 { 1 }, "wrap should cross the uint32 boundary");
}

void test_unwrap_chooses_nearest_cycle()
{
    const Wrap32 isn { 1000 };
    const Wrap32 value { 1006 };
    expect(unwrap(value, isn, 4) == 6, "an early checkpoint should choose absolute six");
    expect(unwrap(value, isn, modulus + 5) == modulus + 6, "a later checkpoint should choose the next cycle");
}

void test_receiver_ignores_data_before_syn()
{
    TcpReceiver receiver(8);
    receiver.receive(TcpSegment { Wrap32 { 1001 }, false, false, "A" });
    expect(!receiver.ackno().has_value(), "a receiver should not acknowledge data before learning an ISN from SYN");
    expect(receiver.reassembler().output().bytes_buffered() == 0, "pre-SYN payload must not enter the ByteStream");
}

void test_syn_establishes_ack_origin()
{
    TcpReceiver receiver(8);
    receiver.receive(TcpSegment { Wrap32 { 5000 }, true, false, "" });
    expect(receiver.ackno() == Wrap32 { 5001 }, "receiving SYN should make ACK equal ISN plus one");
    expect(receiver.window_size() == 8, "a SYN without payload should not consume application receive capacity");
}

void test_syn_with_payload_starts_at_stream_zero()
{
    TcpReceiver receiver(8);
    receiver.receive(TcpSegment { Wrap32 { 4000 }, true, false, "ABC" });
    expect(receiver.reassembler().output().peek(8) == "ABC", "payload carried with SYN should begin at first_index zero");
    expect(receiver.ackno() == Wrap32 { 4004 }, "ACK should cover one SYN and three payload bytes");
}

void test_in_order_payload_ack_and_window()
{
    TcpReceiver receiver(8);
    receiver.receive(TcpSegment { Wrap32 { 1000 }, true, false, "" });
    receiver.receive(TcpSegment { Wrap32 { 1001 }, false, false, "ABC" });
    expect(receiver.reassembler().output().peek(8) == "ABC", "in-order payload should reach the ByteStream");
    expect(receiver.ackno() == Wrap32 { 1004 }, "ACK should point after the contiguous payload");
    expect(receiver.window_size() == 5, "three unread bytes should consume three of eight capacity slots");
}

void test_out_of_order_fin_then_gap_fill()
{
    TcpReceiver receiver(10);
    receiver.receive(TcpSegment { Wrap32 { 1000 }, true, false, "" });
    receiver.receive(TcpSegment { Wrap32 { 1006 }, false, true, "FGHIJ" });
    expect(receiver.ackno() == Wrap32 { 1001 }, "ACK must not cross a gap");
    expect(receiver.reassembler().bytes_pending() == 5 && receiver.window_size() == 5, "out-of-order bytes should be pending and consume capacity");
    expect(!receiver.reassembler().output().is_closed(), "an early FIN must wait for the missing prefix");
    receiver.receive(TcpSegment { Wrap32 { 1001 }, false, false, "ABCDE" });
    expect(receiver.reassembler().output().peek(10) == "ABCDEFGHIJ", "filling the gap should flush the whole stream");
    expect(receiver.reassembler().output().is_closed(), "the Writer should close after reaching the remembered FIN");
    expect(receiver.ackno() == Wrap32 { 1012 }, "ACK should cover SYN, ten bytes, and FIN");
    expect(receiver.window_size() == 0, "ten unread bytes should fill the receive capacity");
}

void test_reader_release_changes_window_not_ack()
{
    TcpReceiver receiver(6);
    receiver.receive(TcpSegment { Wrap32 { 2000 }, true, false, "" });
    receiver.receive(TcpSegment { Wrap32 { 2001 }, false, false, "ABCDEF" });
    const auto ack_before = receiver.ackno();
    expect(receiver.window_size() == 0, "six unread bytes should close a capacity-six window");
    receiver.reassembler().output().pop(2);
    expect(receiver.ackno() == ack_before, "application reads must not move the cumulative ACK");
    expect(receiver.window_size() == 2, "Reader pop should reopen exactly two capacity slots");
}

void test_sequence_wrap_inside_receiver()
{
    const std::uint32_t isn_raw = 0xffff'fffdU;
    TcpReceiver receiver(6);
    receiver.receive(TcpSegment { Wrap32 { isn_raw }, true, false, "" });
    receiver.receive(TcpSegment { Wrap32 { 0 }, false, false, "C" });
    expect(receiver.ackno() == Wrap32 { 0xffff'fffeU }, "an out-of-order wrapped byte must not advance ACK over AB");
    receiver.receive(TcpSegment { Wrap32 { 0xffff'fffeU }, false, false, "AB" });
    expect(receiver.reassembler().output().peek(6) == "ABC", "unwrap should place bytes correctly across the 32-bit boundary");
    expect(receiver.ackno() == Wrap32 { 1 }, "ACK should wrap naturally after SYN and three bytes");
}

void test_empty_stream_fin()
{
    TcpReceiver receiver(4);
    receiver.receive(TcpSegment { Wrap32 { 9000 }, true, true, "" });
    expect(receiver.reassembler().output().is_finished(), "SYN plus FIN with no payload should complete an empty receive stream");
    expect(receiver.ackno() == Wrap32 { 9002 }, "ACK should cover both SYN and FIN");
}

} // namespace

int main()
{
    const std::pair<const char*, std::function<void()>> tests[] {
        { "wrap basic and cycle", test_wrap_basic_and_cycle },
        { "unwrap nearest cycle", test_unwrap_chooses_nearest_cycle },
        { "ignore pre-SYN data", test_receiver_ignores_data_before_syn },
        { "SYN establishes ACK", test_syn_establishes_ack_origin },
        { "SYN with payload", test_syn_with_payload_starts_at_stream_zero },
        { "in-order ACK and window", test_in_order_payload_ack_and_window },
        { "out-of-order FIN and gap", test_out_of_order_fin_then_gap_fill },
        { "Reader release", test_reader_release_changes_window_not_ack },
        { "sequence wrap in receiver", test_sequence_wrap_inside_receiver },
        { "empty stream FIN", test_empty_stream_fin },
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
