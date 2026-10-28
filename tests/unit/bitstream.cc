#include "mdcomp/bitstream.hh"

#include "support/rejecting_output.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace {
    template <typename Word>
    struct writer {
        std::array<Word, 3>* words;
        size_t*              count;

        constexpr void operator()(Word value) const noexcept {
            (*words)[(*count)++] = value;
        }
    };

    template <typename Word>
    struct reader {
        std::array<Word, 3> const* words;
        size_t*                    count;

        constexpr Word operator()() const noexcept {
            return (*words)[(*count)++];
        }
    };

    template <typename Word, bit_endian Order>
    constexpr bool write_case(size_t prefix, size_t count) {
        constexpr size_t                      width = sizeof(Word) * 8;
        std::array<Word, 3>                   actual{}, expected{};
        size_t                                output_count = 0;
        obitbuffer<Word, writer<Word>, Order> output(
                writer<Word>{&actual, &output_count});
        for (size_t i = 0; i < prefix; ++i) {
            output.push(0);
        }
        Word const value = static_cast<Word>(~Word{});
        output.write(value, count);
        output.flush();
        for (size_t i = prefix; i < prefix + count; ++i) {
            size_t const shift
                    = Order == bit_endian::big ? width - 1 - i % width : i % width;
            expected[i / width] |= static_cast<Word>(Word{1} << shift);
        }
        return actual == expected && output_count == (prefix + count + width - 1) / width
               && output.have_waiting_bits() == 0 && !output.flush();
    }

    template <typename Word, bit_endian Order, bool Early>
    constexpr bool read_case(size_t prefix, size_t count) {
        constexpr size_t          width = sizeof(Word) * 8;
        std::array<Word, 3> const words{
                static_cast<Word>(0x89abcdef01234567ULL),
                static_cast<Word>(0x13579bdf2468ace0ULL), Word{}};
        size_t                                       reads = 0;
        ibitbuffer<Word, reader<Word>, Order, Early> input(reader<Word>{&words, &reads});
        for (size_t i = 0; i < prefix; ++i) {
            (void)input.pop();
        }
        auto const before = reads;
        Word const actual = input.read(count);
        Word       expected{};
        for (size_t i = prefix; i < prefix + count; ++i) {
            size_t const shift
                    = Order == bit_endian::big ? width - 1 - i % width : i % width;
            expected = static_cast<Word>(
                    (expected << 1) | ((words[i / width] >> shift) & 1));
        }
        return actual == expected && (count != 0 || reads == before);
    }

    template <typename Word>
    constexpr bool compile_cases() {
        constexpr size_t width = sizeof(Word) * 8;
        for (auto prefix : {size_t{0}, size_t{1}, width - 1, width}) {
            for (auto count : {size_t{0}, size_t{1}, width - 1, width}) {
                if (!write_case<Word, bit_endian::big>(prefix, count)
                    || !write_case<Word, bit_endian::little>(prefix, count)
                    || !read_case<Word, bit_endian::big, false>(prefix, count)
                    || !read_case<Word, bit_endian::little, true>(prefix, count)) {
                    return false;
                }
            }
        }
        return true;
    }

    static_assert(compile_cases<uint8_t>());
    static_assert(compile_cases<uint16_t>());
    static_assert(compile_cases<uint32_t>());
    static_assert(compile_cases<uint64_t>());

    struct throwing_copy {
        throwing_copy() = default;

        throwing_copy(throwing_copy const&) {
            throw std::runtime_error("copy failure");
        }

        uint8_t operator()() const noexcept {
            return 0;
        }

        void operator()(uint8_t) const noexcept {}
    };

    using input_buffer  = ibitbuffer<uint8_t, throwing_copy, bit_endian::big, false>;
    using output_buffer = obitbuffer<uint8_t, throwing_copy, bit_endian::big>;
    static_assert(!std::is_nothrow_constructible_v<input_buffer, throwing_copy const&>);
    static_assert(!std::is_nothrow_constructible_v<output_buffer, throwing_copy const&>);
}    // namespace

TEST_CASE("Bit buffers match a per-bit oracle at word boundaries", "[bits]") {
    for (size_t prefix = 0; prefix <= 64; ++prefix) {
        for (size_t count = 0; count <= 64; ++count) {
            CAPTURE(prefix, count);
            CHECK((write_case<uint64_t, bit_endian::big>(prefix, count)));
            CHECK((write_case<uint64_t, bit_endian::little>(prefix, count)));
            CHECK((read_case<uint64_t, bit_endian::big, false>(prefix, count)));
            CHECK((read_case<uint64_t, bit_endian::little, true>(prefix, count)));
            CHECK((read_case<uint64_t, bit_endian::big, true>(prefix, count)));
            CHECK((read_case<uint64_t, bit_endian::little, false>(prefix, count)));
        }
    }
    CHECK(compile_cases<uint8_t>());
    CHECK(compile_cases<uint16_t>());
    CHECK(compile_cases<uint32_t>());
}

TEST_CASE(
        "Bit buffers validate counts and propagate callback construction errors",
        "[bits]") {
    std::array<uint64_t, 3>                                 words{};
    size_t                                                  calls = 0;
    obitbuffer<uint64_t, writer<uint64_t>, bit_endian::big> output(
            writer<uint64_t>{&words, &calls});
    CHECK_THROWS_AS(output.write(0, 65), std::out_of_range);
    CHECK(calls == 0);
    ibitbuffer<uint64_t, reader<uint64_t>, bit_endian::big, false> input(
            reader<uint64_t>{&words, &calls});
    CHECK_THROWS_AS(input.read(65), std::out_of_range);
    CHECK(input.have_waiting_bits() == 64);
    throwing_copy const callback;
    CHECK_THROWS_AS(input_buffer(callback), std::runtime_error);
    CHECK_THROWS_AS(output_buffer(callback), std::runtime_error);
}

TEST_CASE(
        "Bitstream copies own their callbacks and flush propagates failures", "[bits]") {
    std::ostringstream destination;
    using output_type = obitstream<uint8_t, bit_endian::big, big_endian>;
    auto output       = [&]() {
        output_type original(destination);
        auto        copy = original;
        return copy;
    }();
    output.write(0x12, 8);
    output.write(0x03, 2);
    output.flush();
    CHECK(destination.str() == std::string("\x12\xc0", 2));

    std::istringstream source(std::string("\x12\x34", 2));
    using input_type = ibitstream<uint8_t, bit_endian::big, big_endian, false>;
    auto input       = [&]() {
        input_type original(source);
        auto       copy = original;
        return copy;
    }();
    CHECK(input.read(8) == 0x12);
    CHECK(input.read(8) == 0x34);

    test_support::rejecting_output_buffer buffer;
    std::ostream                          failing_destination(&buffer);
    failing_destination.exceptions(std::ios::badbit);
    output_type failing(failing_destination);
    failing.write(1, 1);
    CHECK_THROWS_AS(failing.flush(), std::ios_base::failure);
    CHECK(failing_destination.bad());
}
