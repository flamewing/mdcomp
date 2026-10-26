#include "mdcomp/stream_utils.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {
    static_assert(utils::round_up(5, 4) == 8);
    static_assert(utils::round_up(-5, 4) == -4);
    static_assert(
            utils::round_up(std::numeric_limits<size_t>::max(), size_t{1})
            == std::numeric_limits<size_t>::max());
    static_assert(
            utils::round_up(std::numeric_limits<int>::min(), 3)
            == std::numeric_limits<int>::min() + 2);

    struct first : utils::range_adaptor_closure<first> {
        template <typename Range>
        constexpr decltype(auto) operator()(Range&& range) const {
            return *std::ranges::begin(range);
        }
    };

    struct identity : utils::range_adaptor_closure<identity> {
        template <typename T>
        constexpr T&& operator()(T&& value) const {
            return std::forward<T>(value);
        }
    };

    constexpr bool reference_pipeline() {
        int  values[]{1, 2};
        auto pipeline = first{} | identity{};
        static_assert(std::same_as<decltype(values | pipeline), int&>);
        (values | pipeline) = 42;
        return values[0] == 42;
    }

    static_assert(reference_pipeline());

    class failed_padding_buffer : public std::streambuf {
        pos_type seekoff(off_type, std::ios::seekdir, std::ios::openmode) override {
            return pos_type{1};
        }

        int_type overflow(int_type) override {
            return traits_type::eof();
        }
    };

    class unseekable_buffer : public std::stringbuf {
        pos_type seekoff(off_type, std::ios::seekdir, std::ios::openmode) override {
            return pos_type{-1};
        }
    };

    class failed_write_buffer : public std::streambuf {
        std::streamsize xsputn(char const*, std::streamsize) override {
            return 0;
        }
    };
}    // namespace

TEST_CASE("Rounding validates factors and representable results", "[stream_utils]") {
    CHECK(utils::round_up(-5, 4) == -4);
    CHECK(utils::round_up(9U, 4U) == 12);
    CHECK(utils::round_up(std::numeric_limits<size_t>::max(), size_t{1})
          == std::numeric_limits<size_t>::max());
    CHECK_THROWS_AS(utils::round_up(3, 0), std::invalid_argument);
    CHECK_THROWS_AS(utils::round_up(3, -1), std::invalid_argument);
    CHECK_THROWS_AS(
            utils::round_up(std::numeric_limits<size_t>::max(), size_t{2}),
            std::overflow_error);
    CHECK_THROWS_AS(
            utils::round_up(std::numeric_limits<int>::max(), 2), std::overflow_error);
}

TEST_CASE("Padding stops on stream failures", "[stream_utils]") {
    std::ostringstream output;
    output << 'a';
    utils::pad_to_multiple(output, 4);
    CHECK(output.str() == std::string("a\0\0\0", 4));
    utils::pad_to_even(output);
    CHECK(output.str().size() == 4);
    CHECK_THROWS_AS(utils::pad_to_multiple(output, 0), std::invalid_argument);
    failed_padding_buffer failed;
    std::ostream          destination(&failed);
    CHECK_THROWS_AS(utils::pad_to_multiple(destination, 4), std::ios_base::failure);
    unseekable_buffer unseekable;
    std::ostream      unknown(&unseekable);
    CHECK_THROWS_AS(utils::pad_to_even(unknown), std::ios_base::failure);
}

TEST_CASE(
        "Stream sizing preserves position and exception-mask behavior",
        "[stream_utils]") {
    std::istringstream source("abcdef");
    source.seekg(2);
    source.exceptions(std::ios::failbit | std::ios::badbit | std::ios::eofbit);
    CHECK(utils::ssize(source) == 4);
    CHECK(source.tellg() == std::streampos{2});
    CHECK(source.good());
    CHECK(utils::size(source) == 4);
    unseekable_buffer buffer;
    std::istream      unknown(&buffer);
    CHECK_THROWS_AS(utils::size(unknown), std::ios_base::failure);
}

TEST_CASE(
        "Raw byte helpers preserve object bytes and propagate writes", "[stream_utils]") {
    std::istringstream source(std::string("\x12\x34\x56", 3));
    auto               bytes = utils::read_from_bytes<std::vector<char>>(source, 3, 4);
    CHECK(bytes == std::vector<char>{0x12, 0x34, 0x56, 0});
    std::ostringstream output;
    utils::write_as_bytes<char>(output, bytes);
    CHECK(output.str() == std::string("\x12\x34\x56\0", 4));
    failed_write_buffer failed;
    std::ostream        destination(&failed);
    destination.exceptions(std::ios::badbit);
    CHECK_THROWS_AS(
            utils::write_as_bytes<char>(destination, bytes), std::ios_base::failure);
}

TEST_CASE(
        "Range conversion recursively converts nested views", "[stream_utils][ranges]") {
    std::array<std::array<int, 2>, 2> source{
            {{1, 2}, {3, 4}}
    };
    auto nested = source | std::views::transform([](auto& row) {
                      return std::span(row);
                  });
    auto values = utils::to<std::vector<std::vector<int>>>(nested);
    CHECK(values
          == std::vector<std::vector<int>>{
                  {1, 2},
                  {3, 4}
    });
    auto flat = source[0] | utils::to<std::vector>();
    CHECK(flat == std::vector<int>{1, 2});
    CHECK(reference_pipeline());
}
