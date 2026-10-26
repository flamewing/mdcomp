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
#include <vector>

static_assert(test_support::fixed_io_cases());
static_assert(noexcept(big_endian::read2(std::declval<unsigned char*&>())));
static_assert(noexcept(big_endian::write2(std::declval<unsigned char*&>(), 0)));

namespace {
    template <bool Nothrow>
    struct output_cursor {
        using difference_type = std::ptrdiff_t;
        unsigned* position;
        bool      fail = false;

        unsigned& operator*() const noexcept(Nothrow) {
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
            std::declval<std::back_insert_iterator<std::vector<unsigned>>&>(), 0)));
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

TEST_CASE("Containers append unsigned byte values in the requested order", "[endian]") {
    std::vector<unsigned char> bytes{0xa5};
    big_endian::write2(bytes, 0x80ff);
    little_endian::write2(bytes, 0x80ff);
    CHECK(bytes == std::vector<unsigned char>{0xa5, 0x80, 0xff, 0xff, 0x80});

    std::vector<unsigned> wide{0xa5};
    big_endian::write2(wide, 0x80ff);
    little_endian::write2(wide, 0x80ff);
    CHECK(wide == std::vector<unsigned>{0xa5, 0x80, 0xff, 0xff, 0x80});

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
    unsigned            bytes[4]{};
    output_cursor<true> output{bytes};
    big_endian::write2(output, 0x80ff);
    big_endian::write2(output, 0x1234);
    CHECK(output.position == bytes + 4);
    CHECK(std::vector<unsigned>(bytes, bytes + 4)
          == std::vector<unsigned>{0x80, 0xff, 0x12, 0x34});

    output_cursor<false> throwing{bytes, true};
    CHECK_THROWS_AS(big_endian::write2(throwing, 0x1234), std::runtime_error);
    CHECK(throwing.position == bytes);
}
