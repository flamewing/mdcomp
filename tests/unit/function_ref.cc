#include "mdcomp/function_ref.hh"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <type_traits>

namespace {
    constexpr int increment(int value) {
        return value + 1;
    }

    int decrement(int value) {
        return value - 1;
    }

    struct callable {
        int value = 7;

        int add(int other) {
            return value += other;
        }

        int read(int other) const noexcept {
            return value + other;
        }
    };

    constexpr bool constant_call() {
        std23::function_ref<int(int)> function(std23::nontype<increment>);
        return function(4) == 5;
    }

    static_assert(constant_call());
    static_assert(std::is_trivially_copyable_v<std23::function_ref<int(int)>>);
}    // namespace

TEST_CASE("Function references forward value and reference arguments", "[function_ref]") {
    auto take = [](std::unique_ptr<int> value) {
        return *value;
    };
    std23::function_ref<int(std::unique_ptr<int>)> function(take);
    CHECK(function(std::make_unique<int>(42)) == 42);
    auto modify = [](std::string&& value) {
        value += "!";
    };
    std23::function_ref<void(std::string&&)> modifier(modify);
    std::string                              value = "test";
    modifier(std::move(value));
    CHECK(value == "test!");
}

TEST_CASE("Function references store function pointer values", "[function_ref]") {
    auto                          pointer = &increment;
    std23::function_ref<int(int)> function(pointer);
    pointer = &decrement;
    CHECK(function(4) == 5);
    function = pointer;
    CHECK(function(4) == 3);
    CHECK(constant_call());
}

TEST_CASE(
        "Bound function references support pointer temporaries and cv qualifiers",
        "[function_ref]") {
    callable                      object;
    std23::function_ref<int(int)> modify(std23::nontype<&callable::add>, &object);
    CHECK(modify(2) == 9);
    std23::function_ref<int(int) const noexcept> read(
            std23::nontype<&callable::read>, &object);
    static_assert(noexcept(read(1)));
    CHECK(read(1) == 10);
    callable const                               fixed;
    std23::function_ref<int(int) const noexcept> const_read(
            std23::nontype<&callable::read>, &fixed);
    CHECK(const_read(1) == 8);
    std23::function_ref<int&()> member(std23::nontype<&callable::value>, &object);
    member() = 42;
    CHECK(object.value == 42);
}
