#include "tiny_stream.hh"

#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using tinystream::ByteStream;

void expect(const bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_push_peek_pop()
{
    ByteStream stream(8);
    expect(stream.push("ABCDE") == 5, "initial push should accept all bytes");
    expect(stream.peek(3) == "ABC", "peek should return the prefix");
    expect(stream.bytes_buffered() == 5, "peek must not consume bytes");
    expect(stream.pop(2) == 2, "pop should consume two bytes");
    expect(stream.peek(8) == "CDE", "remaining bytes should keep order");
}

void test_partial_push_and_capacity()
{
    ByteStream stream(6);
    expect(stream.push("ABC") == 3, "first push should accept three bytes");
    expect(stream.available_capacity() == 3, "three slots should remain");
    expect(stream.push("DEFGH") == 3, "second push should accept only the prefix that fits");
    expect(stream.peek(10) == "ABCDEF", "accepted prefix should be in the buffer");
    expect(stream.bytes_pushed() == 6, "pushed counter should count accepted bytes only");
}

void test_close_preserves_buffer_and_rejects_future_data()
{
    ByteStream stream(4);
    expect(stream.push("XYZ") == 3, "setup push should succeed");
    stream.close();
    expect(stream.is_closed(), "close should change the closed state");
    expect(stream.peek(4) == "XYZ", "close must preserve unread bytes");
    expect(stream.push("A") == 0, "closed stream must reject new bytes");
    expect(!stream.is_finished(), "closed non-empty stream is not finished");
}

void test_empty_open_is_not_finished()
{
    ByteStream stream(2);
    expect(stream.bytes_buffered() == 0, "new stream should be empty");
    expect(!stream.is_finished(), "empty open stream is not finished");
    stream.close();
    expect(stream.is_finished(), "closed empty stream should be finished");
}

void test_pop_clamps_and_counters()
{
    ByteStream stream(5);
    expect(stream.push("ABCDE") == 5, "setup push should fill stream");
    expect(stream.pop(99) == 5, "pop should clamp to available bytes");
    expect(stream.bytes_buffered() == 0, "all bytes should be consumed");
    expect(stream.bytes_popped() == 5, "popped counter should count consumed bytes");
    expect(stream.bytes_pushed() - stream.bytes_popped() == stream.bytes_buffered(), "accounting invariant should hold");
}

void test_binary_bytes_and_empty_operations()
{
    ByteStream stream(3);
    const std::string binary { '\0', '\x7f', '\xff' };
    expect(stream.push(binary) == 3, "binary bytes including zero should be accepted");
    expect(stream.peek(3) == binary, "binary bytes should keep their values");
    expect(stream.pop(0) == 0, "pop zero should do nothing");
    expect(stream.peek(0).empty(), "peek zero should return empty");
}

void test_full_state_trace()
{
    ByteStream stream(4);
    expect(stream.push("WXYZ") == 4, "first push should fill capacity four");
    expect(stream.pop(2) == 2, "pop should free two slots");
    expect(stream.push("123") == 2, "second push should accept two bytes");
    expect(stream.peek(8) == "YZ12", "trace should preserve FIFO order");
    stream.close();
    expect(stream.pop(99) == 4, "final pop should drain the remaining four bytes");
    expect(stream.is_finished(), "closed and empty stream should finish");
    expect(stream.bytes_pushed() == 6 && stream.bytes_popped() == 6, "trace counters should match");
}

} // namespace

int main()
{
    const std::pair<const char*, std::function<void()>> tests[] {
        {"push, peek, and pop", test_push_peek_pop},
        {"partial push and capacity", test_partial_push_and_capacity},
        {"close preserves buffer", test_close_preserves_buffer_and_rejects_future_data},
        {"empty open versus finished", test_empty_open_is_not_finished},
        {"pop clamp and counters", test_pop_clamps_and_counters},
        {"binary bytes and empty operations", test_binary_bytes_and_empty_operations},
        {"full state trace", test_full_state_trace},
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
