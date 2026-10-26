#include "mdcomp/bigendian_io.hh"

#include "support/endian_cases.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>
#include <forward_list>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

static_assert(test_support::fixed_io_cases());
static_assert(noexcept(big_endian::read2(std::declval<unsigned char*&>())));
static_assert(noexcept(big_endian::write2(std::declval<unsigned char*&>(), 0)));

namespace {
    template <bool Nothrow>
    struct input_cursor {
        using difference_type = std::ptrdiff_t;
        using value_type      = unsigned char;
        unsigned char const* position;
        bool                 fail = false;

        explicit input_cursor(unsigned char const* pointer, bool should_fail = false)
                : position(pointer), fail(should_fail) {}

        input_cursor(input_cursor&&)            = default;
        input_cursor& operator=(input_cursor&&) = default;

        unsigned char operator*() const noexcept(Nothrow) {
            if constexpr (!Nothrow) {
                if (fail) {
                    throw std::runtime_error("input failure");
                }
            }
            return *position;
        }

        input_cursor& operator++() noexcept {
            ++position;
            return *this;
        }

        void operator++(int) noexcept {
            ++*this;
        }
    };

    static_assert(std::input_iterator<input_cursor<true>>);
    static_assert(!std::copyable<input_cursor<true>>);
    static_assert(noexcept(big_endian::read2(std::declval<input_cursor<true>&>())));
    static_assert(!noexcept(big_endian::read2(std::declval<input_cursor<false>&>())));

    template <bool Nothrow>
    struct output_cursor {
        using difference_type   = std::ptrdiff_t;
        using value_type        = unsigned char;
        using iterator_category = std::output_iterator_tag;
        unsigned char* position;
        bool           fail = false;

        unsigned char& operator*() const noexcept(Nothrow) {
            if constexpr (!Nothrow) {
                if (fail) {
                    throw std::runtime_error("output failure");
                }
            }
            return *position;
        }

        output_cursor& operator++() noexcept {
            ++position;
            return *this;
        }

        output_cursor operator++(int) noexcept {
            auto copy = *this;
            ++*this;
            return copy;
        }
    };

    static_assert(std::output_iterator<output_cursor<true>, unsigned char>);
    static_assert(!std::forward_iterator<output_cursor<true>>);
    static_assert(noexcept(big_endian::write2(std::declval<output_cursor<true>&>(), 0)));
    static_assert(
            !noexcept(big_endian::write2(std::declval<output_cursor<false>&>(), 0)));
    static_assert(!noexcept(big_endian::write2(
            std::declval<std::back_insert_iterator<std::vector<unsigned char>>&>(), 0)));

    template <typename Dst>
    concept word_writable = requires(Dst& output) { big_endian::write2(output, 0x1234); };
    static_assert(word_writable<unsigned char*>);
    static_assert(word_writable<signed char*>);
    static_assert(word_writable<char*>);
    static_assert(word_writable<std::vector<unsigned char>>);
    static_assert(word_writable<std::back_insert_iterator<std::vector<char>>>);
    static_assert(word_writable<std::ostream_iterator<char>>);
    static_assert(!word_writable<unsigned*>);
    static_assert(!word_writable<std::vector<unsigned>>);
    static_assert(!word_writable<std::back_insert_iterator<std::vector<unsigned>>>);
    static_assert(!word_writable<std::ostream_iterator<unsigned>>);
    static_assert(!utils::byte_output_iterator<bool*>);
}    // namespace

TEST_CASE("Byte swapping agrees with explicit known answers", "[endian]") {
    auto const value    = GENERATE(uint64_t{0}, uint64_t{0x0123456789abcdefULL});
    auto const expected = value == 0 ? uint64_t{0} : uint64_t{0xefcdab8967452301ULL};
    CHECK(utils::fallback_byteswap(value) == expected);
    CHECK(utils::byteswap_impl(value) == expected);
    CHECK(utils::byteswap(value) == expected);
}

TEST_CASE("Fixed endian transfers match known bytes at runtime", "[endian]") {
    CHECK(test_support::fixed_io_cases());
}

TEST_CASE("Byte containers append in the requested order", "[endian]") {
    std::vector<unsigned char> bytes{0xa5};
    big_endian::write2(bytes, 0x80ff);
    little_endian::write2(bytes, 0x80ff);
    CHECK(bytes == std::vector<unsigned char>{0xa5, 0x80, 0xff, 0xff, 0x80});

    std::string chars;
    big_endian::write2(chars, 0x80ff);
    CHECK(chars == std::string("\x80\xff", 2));
}

TEST_CASE("Single pass and forward input iterators retain their position", "[endian]") {
    std::istringstream          stream(std::string("\x11\x22\x33\x44", 4));
    std::istream_iterator<char> input(stream);
    CHECK(big_endian::read2(input) == 0x1122);
    CHECK(big_endian::read2(input) == 0x3344);
    CHECK(input == std::istream_iterator<char>{});

    std::forward_list<unsigned char> bytes{0x11, 0x22, 0x33, 0x44};
    auto                             forward = bytes.begin();
    CHECK(big_endian::read2(forward) == 0x1122);
    CHECK(big_endian::read2(forward) == 0x3344);
    CHECK(forward == bytes.end());
}

TEST_CASE("Output-only iterators retain position and propagate failures", "[endian]") {
    unsigned char       bytes[4]{};
    output_cursor<true> output{bytes};
    big_endian::write2(output, 0x80ff);
    big_endian::write2(output, 0x1234);
    CHECK(output.position == bytes + 4);
    CHECK(std::vector<unsigned char>(bytes, bytes + 4)
          == std::vector<unsigned char>{0x80, 0xff, 0x12, 0x34});

    output_cursor<false> throwing{bytes, true};
    CHECK_THROWS_AS(big_endian::write2(throwing, 0x1234), std::runtime_error);
    CHECK(throwing.position == bytes);
}

TEST_CASE(
        "Move-only input iterators retain position and propagate failures", "[endian]") {
    unsigned char const bytes[]{0x80, 0xff, 0x12, 0x34};
    input_cursor<true>  input(bytes);
    CHECK(big_endian::read2(input) == 0x80ff);
    CHECK(big_endian::read2(input) == 0x1234);
    CHECK(input.position == bytes + 4);

    input_cursor<false> throwing(bytes, true);
    CHECK_THROWS_AS(big_endian::read2(throwing), std::runtime_error);
    CHECK(throwing.position == bytes);
}

TEST_CASE("Byte swapping covers every 16-bit value", "[endian]") {
    for (uint32_t raw = 0; raw <= 0xffff; ++raw) {
        auto const value    = static_cast<uint16_t>(raw);
        auto const expected = static_cast<uint16_t>((raw >> 8) | ((raw & 0xff) << 8));
        CAPTURE(raw);
        CHECK(utils::fallback_byteswap(value) == expected);
        CHECK(utils::byteswap_impl(value) == expected);
        CHECK(utils::byteswap(value) == expected);
    }
}

TEST_CASE("Runtime byte swapping matches reversed object bytes", "[endian]") {
    uint64_t state = 0x8b5ad4cef1236790ULL;
    for (size_t i = 0; i < 1024; ++i) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        auto bytes = std::bit_cast<std::array<unsigned char, 8>>(state);
        std::ranges::reverse(bytes);
        auto const expected = std::bit_cast<uint64_t>(bytes);
        CAPTURE(i, state);
        CHECK(utils::fallback_byteswap(state) == expected);
        CHECK(utils::byteswap_impl(state) == expected);
        CHECK(utils::byteswap(state) == expected);
    }
}
