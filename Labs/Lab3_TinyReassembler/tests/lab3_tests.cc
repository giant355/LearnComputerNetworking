#include "tiny_reassembler.hh"

#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using tinyreassembler::Reassembler;

void expect(const bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void test_in_order_delivery()
{
    Reassembler reassembler(8);
    reassembler.insert(0, "ABC", false);
    expect(reassembler.output().peek(8) == "ABC", "an in-order fragment should reach the ByteStream");
    expect(reassembler.first_unassembled() == 3, "the next missing position should be three");
    expect(reassembler.bytes_pending() == 0, "delivered bytes must not remain pending");
}

void test_out_of_order_gap_then_flush()
{
    Reassembler reassembler(8);
    reassembler.insert(3, "DEF", false);
    expect(reassembler.output().bytes_buffered() == 0, "a fragment after a gap must wait");
    expect(reassembler.bytes_pending() == 3, "the useful out-of-order bytes should be pending");
    reassembler.insert(0, "ABC", false);
    expect(reassembler.output().peek(8) == "ABCDEF", "filling the gap should flush both ranges in order");
    expect(reassembler.first_unassembled() == 6 && reassembler.bytes_pending() == 0, "the continuous prefix should advance to six");
}

void test_overlap_and_duplicate()
{
    Reassembler reassembler(10);
    reassembler.insert(0, "ABCDE", false);
    reassembler.insert(3, "DEFG", false);
    expect(reassembler.output().peek(10) == "ABCDEFG", "overlap should contribute only new logical positions");
    reassembler.insert(0, "ABCDE", false);
    expect(reassembler.output().peek(10) == "ABCDEFG", "an already delivered duplicate must be ignored");
    expect(reassembler.first_unassembled() == 7 && reassembler.bytes_pending() == 0, "duplicates must not change counters");
}

void test_trim_already_delivered_prefix()
{
    Reassembler reassembler(8);
    reassembler.insert(0, "ABCD", false);
    expect(reassembler.output().pop(4) == 4, "setup should let the Reader consume ABCD");
    reassembler.insert(2, "CDEFG", false);
    expect(reassembler.output().peek(8) == "EFG", "only the new suffix after the delivered prefix should be pushed");
    expect(reassembler.first_unassembled() == 7, "Reader pop must not make the logical position go backward");
}

void test_capacity_window_and_reader_release()
{
    Reassembler reassembler(6);
    reassembler.insert(3, "DEF", false);
    reassembler.insert(0, "ABC", false);
    expect(reassembler.output().peek(6) == "ABCDEF", "the capacity-six window should assemble its full prefix");
    expect(reassembler.output().pop(3) == 3, "Reader should free three positions");
    reassembler.insert(6, "GHIJ", false);
    expect(reassembler.output().peek(8) == "DEFGHI", "only GHI should fit in the newly opened window");
    expect(reassembler.first_unassembled() == 9, "the clipped J must not advance the logical position");
    reassembler.output().pop(6);
    reassembler.insert(9, "J", false);
    expect(reassembler.output().peek(2) == "J" && reassembler.first_unassembled() == 10, "a discarded window suffix must be accepted when retransmitted later");
}

void test_eof_before_gap_and_half_close()
{
    Reassembler reassembler(10);
    reassembler.insert(5, "WORLD", true);
    expect(reassembler.eof_index() == 10, "the original last fragment should record EOF at ten");
    expect(!reassembler.output().is_closed(), "seeing EOF before the gap is filled must not close the Writer");
    reassembler.insert(0, "HELLO", false);
    expect(reassembler.output().peek(10) == "HELLOWORLD", "filling the gap should assemble through EOF");
    expect(reassembler.output().is_closed(), "the Writer should close when first_unassembled reaches EOF");
    expect(!reassembler.output().is_finished(), "closed with unread bytes is not finished");
    reassembler.output().pop(10);
    expect(reassembler.output().is_finished(), "Reader drain after close should finish the stream");
}

void test_empty_last_fragment()
{
    Reassembler reassembler(4);
    reassembler.insert(0, "", true);
    expect(reassembler.eof_index() == 0, "an empty last fragment should record EOF at zero");
    expect(reassembler.output().is_closed() && reassembler.output().is_finished(), "an empty stream can close and finish immediately");
}

void test_far_future_and_window_suffix_are_discarded()
{
    Reassembler reassembler(4);
    reassembler.insert(100, "Z", false);
    expect(reassembler.bytes_pending() == 0, "a far-future byte outside the window must not consume memory");
    reassembler.insert(2, "CDEF", false);
    expect(reassembler.bytes_pending() == 2, "only positions two and three fit in the initial window");
    reassembler.insert(0, "AB", false);
    expect(reassembler.output().peek(8) == "ABCD", "the accepted window prefix should assemble in order");
    expect(reassembler.first_unassembled() == 4, "the clipped E and F must not be counted");
}

} // namespace

int main()
{
    const std::pair<const char*, std::function<void()>> tests[] {
        {"in-order delivery", test_in_order_delivery},
        {"out-of-order gap and flush", test_out_of_order_gap_then_flush},
        {"overlap and duplicate", test_overlap_and_duplicate},
        {"trim delivered prefix", test_trim_already_delivered_prefix},
        {"capacity window and Reader release", test_capacity_window_and_reader_release},
        {"EOF before gap and half-close", test_eof_before_gap_and_half_close},
        {"empty last fragment", test_empty_last_fragment},
        {"far future and window suffix", test_far_future_and_window_suffix_are_discarded},
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
