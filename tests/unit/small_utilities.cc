#include "mdcomp/forge_span.hh"
#include "mdcomp/ignore_unused_variable_warning.hh"
#include "mdcomp/unreachable.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>

namespace {
    constexpr bool spans() {
        int  values[]{1, 2, 3};
        auto span = unsafe_forge_span(values, 3);
        return span.size() == 3 && span[2] == 3
               && unsafe_forge_span(static_cast<int*>(nullptr), size_t{0}).empty();
    }

    static_assert(spans());
    static_assert(noexcept(ignore_unused_variable_warning(1, 2)));
    static_assert(noexcept(utils::assume(true)));
    static_assert(noexcept(utils::unreachable()));
}    // namespace

TEST_CASE("Span forging validates counts and supports null empty ranges", "[utilities]") {
    CHECK(spans());
    int value{};
    CHECK_THROWS_AS(unsafe_forge_span(&value, -1), std::invalid_argument);
    CHECK_THROWS_AS(
            unsafe_forge_span(&value, std::numeric_limits<size_t>::max()),
            std::length_error);
    utils::assume(true);
}
