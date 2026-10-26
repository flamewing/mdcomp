#include "mdcomp/bigendian_io.hh"
#include "support/endian_cases.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <sstream>
#include <vector>

#if MDCOMP_HAS_INT128
namespace {
    using utils::int128_t;
    using utils::uint128_t;

    constexpr uint128_t value
            = (uint128_t{0x0123456789abcdefULL} << 64) | uint128_t{0xfedcba9876543210ULL};
    constexpr uint128_t swapped
            = (uint128_t{0x1032547698badcfeULL} << 64) | uint128_t{0xefcdab8967452301ULL};
    constexpr std::array<unsigned char, 16> big_bytes{0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
                                                      0xcd, 0xef, 0xfe, 0xdc, 0xba, 0x98,
                                                      0x76, 0x54, 0x32, 0x10};
    constexpr std::array<unsigned char, 16> little_bytes{
            0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
            0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01};

    static_assert(utils::integral<int128_t>);
    static_assert(utils::integral<uint128_t const>);
    static_assert(utils::signed_integral<int128_t>);
    static_assert(utils::unsigned_integral<uint128_t>);
    static_assert(!utils::unsigned_integral<int128_t>);
    static_assert(utils::integral<int> == std::integral<int>);
    static_assert(!utils::integral<float>);
    static_assert(utils::fallback_byteswap(value) == swapped);
    static_assert(utils::byteswap_impl(value) == swapped);
    static_assert(utils::byteswap(value) == swapped);
    static_assert(
            utils::byteswap(std::bit_cast<int128_t>(value))
            == std::bit_cast<int128_t>(swapped));
    static_assert(test_support::fixed_io<big_endian>(value, big_bytes));
    static_assert(test_support::fixed_io<little_endian>(value, little_bytes));

    constexpr bool named_and_sized_io() {
        std::array<unsigned char, 16> bytes{};
        auto*                         output = bytes.data();
        big_endian::write16(output, value);
        auto const* input = bytes.data();
        if (big_endian::read16(input) != value || input != output || bytes != big_bytes) {
            return false;
        }
        output = bytes.data();
        little_endian::write_n<16>(output, value);
        input = bytes.data();
        return little_endian::read_n<16>(input) == value && input == output
               && bytes == little_bytes;
    }

    static_assert(named_and_sized_io());
}    // namespace

TEST_CASE("128-bit byte swapping supports both public signednesses", "[endian][int128]") {
    CHECK(utils::fallback_byteswap(value) == swapped);
    CHECK(utils::byteswap_impl(value) == swapped);
    CHECK(utils::byteswap(value) == swapped);
    CHECK(utils::byteswap(std::bit_cast<int128_t>(value))
          == std::bit_cast<int128_t>(swapped));
    CHECK(utils::byteswap(uint128_t{}) == uint128_t{});
    CHECK(utils::byteswap(~uint128_t{}) == ~uint128_t{});
}

TEST_CASE(
        "128-bit endian I/O has typed named and sized entry points", "[endian][int128]") {
    CHECK(test_support::fixed_io<big_endian>(value, big_bytes));
    CHECK(test_support::fixed_io<little_endian>(value, little_bytes));
    CHECK(named_and_sized_io());

    std::vector<unsigned char> output;
    big_endian::write16(output, value);
    CHECK(output == std::vector<unsigned char>(big_bytes.begin(), big_bytes.end()));

    std::stringstream stream;
    big_endian::write(stream, std::bit_cast<int128_t>(swapped));
    int128_t result{};
    big_endian::read(stream, result);
    CHECK(result == std::bit_cast<int128_t>(swapped));
    CHECK(stream.good());

    endian_output_iterator<big_endian, uint128_t> stream_output(stream);
    *stream_output = value;
    endian_input_iterator<big_endian, uint128_t> stream_input(stream);
    REQUIRE(stream_input != std::default_sentinel);
    CHECK(*stream_input == value);
    ++stream_input;
    CHECK(stream_input == std::default_sentinel);
}
#endif
