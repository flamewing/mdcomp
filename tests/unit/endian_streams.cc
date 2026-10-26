#include "mdcomp/bigendian_io.hh"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
    template <typename T>
    concept reference_readable = requires(unsigned char*& input, T& value) {
        big_endian::read(input, value);
    };
    template <typename T>
    concept value_readable
            = requires(unsigned char*& input) { big_endian::read<T>(input); };
    template <typename T>
    concept writable = requires(unsigned char*& output, T value) {
        big_endian::write(output, value);
    };
    static_assert(reference_readable<int16_t>);
    static_assert(!reference_readable<bool>);
    static_assert(!value_readable<bool>);
    static_assert(!writable<bool>);

    constexpr bool signed_read() {
        unsigned char const bytes[]{0x80, 0x00};
        auto const*         input = bytes;
        int16_t             value{};
        big_endian::read(input, value);
        return value == -32768 && input == bytes + 2;
    }

    static_assert(signed_read());

    using input_iterator  = endian_input_iterator<big_endian, uint16_t>;
    using output_iterator = endian_output_iterator<big_endian, uint16_t>;
    static_assert(std::sentinel_for<std::default_sentinel_t, input_iterator>);
    static_assert(!noexcept(++std::declval<input_iterator&>()));
    static_assert(!noexcept(std::declval<input_iterator&>()++));
    static_assert(!noexcept(std::declval<output_iterator&>() = uint16_t{}));
    static_assert(!noexcept(big_endian::read2(std::declval<std::streambuf&>())));
    static_assert(!noexcept(big_endian::write2(std::declval<std::streambuf&>(), 0)));

    class short_output_buffer : public std::stringbuf {
        std::streamsize xsputn(char const* data, std::streamsize count) override {
            return std::stringbuf::xsputn(data, std::min(count, std::streamsize{1}));
        }
    };

    struct qualified_stream {
        qualified_stream& read(char*, std::streamsize) & noexcept {
            return *this;
        }

        qualified_stream& read(char*, std::streamsize) && {
            throw std::runtime_error("rvalue read failure");
        }

        qualified_stream& write(char const*, std::streamsize) & noexcept {
            return *this;
        }

        qualified_stream& write(char const*, std::streamsize) && {
            throw std::runtime_error("rvalue write failure");
        }
    };

    static_assert(noexcept(big_endian::read2(std::declval<qualified_stream&>())));
    static_assert(!noexcept(big_endian::read2(std::declval<qualified_stream>())));
    static_assert(noexcept(big_endian::write2(std::declval<qualified_stream&>(), 0)));
    static_assert(!noexcept(big_endian::write2(std::declval<qualified_stream>(), 0)));

    class throwing_streambuf : public std::streambuf {
        std::streamsize xsgetn(char*, std::streamsize) override {
            throw std::runtime_error("streambuf read failure");
        }

        std::streamsize xsputn(char const*, std::streamsize) override {
            throw std::runtime_error("streambuf write failure");
        }
    };
}    // namespace

TEST_CASE("Signed output-reference reads match typed reads", "[endian][stream]") {
    CHECK(signed_read());
    std::istringstream stream(std::string("\xff\xff\x80\x00", 4));
    int16_t            value{};
    big_endian::read(stream, value);
    CHECK(value == -1);
    CHECK(big_endian::read<int16_t>(stream) == -32768);
}

TEST_CASE(
        "Endian input iterators prime the cache and reach both end forms",
        "[endian][stream]") {
    std::istringstream stream(std::string("\x12\x34\x80\xff", 4));
    input_iterator     input(stream);
    CHECK(input != std::default_sentinel);
    CHECK(*input == 0x1234);
    CHECK(*input++ == 0x1234);
    CHECK(*input == 0x80ff);
    ++input;
    CHECK(input == std::default_sentinel);
    CHECK(input == input_iterator{});
    CHECK(input_iterator{} == std::default_sentinel);

    std::istringstream    range_stream(std::string("\x12\x34\x80\xff", 4));
    std::vector<uint16_t> values;
    std::ranges::copy(
            input_iterator(range_stream), std::default_sentinel,
            std::back_inserter(values));
    CHECK(values == std::vector<uint16_t>{0x1234, 0x80ff});
}

TEST_CASE(
        "Endian input iterators discard empty and incomplete values",
        "[endian][stream]") {
    for (auto const& bytes : {std::string{}, std::string("\x12", 1)}) {
        std::istringstream stream(bytes);
        CHECK(input_iterator(stream) == std::default_sentinel);
    }
    std::istringstream stream(std::string("\x12\x34\x56", 3));
    input_iterator     input(stream);
    REQUIRE(*input == 0x1234);
    ++input;
    CHECK(input == std::default_sentinel);
}

TEST_CASE("Stream adapters propagate configured stream exceptions", "[endian][stream]") {
    std::istringstream empty;
    empty.exceptions(std::ios::failbit | std::ios::badbit);
    CHECK_THROWS_AS(input_iterator(empty), std::ios_base::failure);

    std::istringstream stream(std::string("\x12\x34", 2));
    input_iterator     input(stream);
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    CHECK_THROWS_AS(++input, std::ios_base::failure);

    short_output_buffer buffer;
    std::ostream        output(&buffer);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output_iterator iterator(output);
    CHECK_THROWS_AS(iterator = uint16_t{0x1234}, std::ios_base::failure);
}

TEST_CASE("Streambuf transfers report short reads and writes", "[endian][stream]") {
    std::stringbuf complete(std::string("\x12\x34", 2));
    CHECK(big_endian::read2(complete) == 0x1234);
    for (auto const& bytes : {std::string{}, std::string("\x12", 1)}) {
        std::stringbuf short_input(bytes);
        CHECK_THROWS_AS(big_endian::read2(short_input), std::ios_base::failure);
    }

    std::stringbuf output;
    big_endian::write2(output, 0x80ff);
    CHECK(output.str() == std::string("\x80\xff", 2));
    short_output_buffer short_output;
    CHECK_THROWS_AS(big_endian::write2(short_output, 0x80ff), std::ios_base::failure);
    CHECK(short_output.str() == std::string("\x80", 1));
}

TEST_CASE(
        "Ordinary streams preserve state-based short-transfer reporting",
        "[endian][stream]") {
    std::istringstream input(std::string("\x12", 1));
    CHECK(big_endian::read2(input) == 0x1200);
    CHECK(input.fail());
    CHECK(input.eof());

    short_output_buffer buffer;
    std::ostream        output(&buffer);
    big_endian::write2(output, 0x1234);
    CHECK(output.bad());
}

TEST_CASE(
        "Stream forwarding preserves ref-qualified exception behavior",
        "[endian][stream]") {
    qualified_stream stream;
    CHECK(big_endian::read2(stream) == 0);
    CHECK_NOTHROW(big_endian::write2(stream, 0x1234));
    CHECK_THROWS_AS(big_endian::read2(std::move(stream)), std::runtime_error);
    CHECK_THROWS_AS(big_endian::write2(std::move(stream), 0x1234), std::runtime_error);

    throwing_streambuf buffer;
    CHECK_THROWS_AS(big_endian::read2(buffer), std::runtime_error);
    CHECK_THROWS_AS(big_endian::write2(buffer, 0x1234), std::runtime_error);
}
