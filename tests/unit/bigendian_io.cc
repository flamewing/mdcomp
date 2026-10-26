#include "mdcomp/bigendian_io.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>

TEST_CASE("Byte swapping agrees with explicit known answers", "[endian]") {
    auto const value = GENERATE(uint64_t{0}, uint64_t{0x0123456789abcdefULL});
    auto const expected = value == 0 ? uint64_t{0} : uint64_t{0xefcdab8967452301ULL};
    CHECK(utils::fallback_byteswap(value) == expected);
    CHECK(utils::byteswap_impl(value) == expected);
    CHECK(utils::byteswap(value) == expected);
}
