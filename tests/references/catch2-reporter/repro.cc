#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

TEST_CASE("Large binary vector assertion rendering") {
    std::vector<uint8_t> actual(65536, 0xa5), expected(65536, 0xa5);
    for (size_t i = 4612; i < 8704; i += 4) {
        actual[i] = actual[i + 1] = 0xff;
    }
    CHECK(actual == expected);
}
