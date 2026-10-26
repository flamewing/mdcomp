#include "mdcomp/basic_decoder.hh"
#include "mdcomp/moduled_adaptor.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace {
    struct format : basic_decoder<format, pad_mode::pad_even>,
                    moduled_adaptor<format, 4, 4> {
        static inline bool              encode_ok     = true;
        static inline bool              decode_ok     = true;
        static inline bool              make_progress = true;
        static inline std::vector<char> last_input;

        static bool encode(std::ostream& dest, std::span<char const> bytes) {
            last_input.assign(bytes.begin(), bytes.end());
            if (!encode_ok) {
                return false;
            }
            write1(dest, static_cast<uint8_t>(bytes.size()));
            utils::write_as_bytes<char>(dest, bytes);
            return bool(dest);
        }

        static bool decode(std::istream& source, std::iostream& dest) {
            if (!decode_ok) {
                return false;
            }
            if (!make_progress) {
                return true;
            }
            auto const size  = read1(source);
            auto       bytes = utils::read_from_bytes<std::vector<char>>(source, size);
            if (!source) {
                return false;
            }
            utils::write_as_bytes<char>(dest, bytes);
            return bool(dest);
        }
    };

    struct reset_format {
        reset_format() {
            format::encode_ok = format::decode_ok = format::make_progress = true;
        }

        ~reset_format() {
            format::encode_ok = format::decode_ok = format::make_progress = true;
        }
    };

    static_assert(aligned_allocator<char, 16>{} == aligned_allocator<int, 16>{});
    static_assert(!(aligned_allocator<char, 16>{} == aligned_allocator<char, 32>{}));
}    // namespace

TEST_CASE(
        "Aligned allocation validates sizes and preserves allocator identity",
        "[decoder]") {
    aligned_allocator<uint32_t, 64> allocator;
    auto*                           pointer = allocator.allocate(7);
    CHECK(reinterpret_cast<uintptr_t>(pointer) % 64 == 0);
    allocator.deallocate(pointer, 7);
    CHECK_THROWS_AS(
            allocator.allocate(std::numeric_limits<size_t>::max()),
            std::bad_array_new_length);
}

TEST_CASE("Basic decoder wrappers pad input and handle empty extraction", "[decoder]") {
    reset_format       reset;
    std::istringstream source("abc");
    std::ostringstream dest;
    CHECK(basic_decoder<format, pad_mode::pad_even>::encode(source, dest));
    CHECK(format::last_input == std::vector<char>{'a', 'b', 'c', 0});
    std::istringstream empty;
    std::stringstream  extracted;
    basic_decoder<format, pad_mode::pad_even>::extract(empty, extracted);
    CHECK(extracted.good());
    CHECK(extracted.str().empty());

    format::encode_ok = false;
    std::istringstream failed_input("abc");
    CHECK_FALSE(basic_decoder<format, pad_mode::pad_even>::encode(failed_input, dest));
}

TEST_CASE(
        "Moduled wrappers preserve payloads across module boundaries",
        "[decoder][moduled]") {
    reset_format reset;
    for (auto size : {size_t{0}, size_t{3}, size_t{4}, size_t{5}, size_t{8}, size_t{9}}) {
        CAPTURE(size);
        std::string const  bytes(size, 'x');
        std::istringstream source(bytes);
        std::stringstream  compressed;
        REQUIRE(format::moduled_encode(source, compressed));
        CHECK(static_cast<unsigned char>(compressed.str()[0]) == 0);
        CHECK(static_cast<unsigned char>(compressed.str()[1]) == size);
        std::stringstream dest;
        dest << "prefix";
        REQUIRE(format::moduled_decode(compressed, dest));
        CHECK(dest.str() == "prefix" + bytes);
    }
}

TEST_CASE("Moduled wrappers detect failure and lack of progress", "[decoder][moduled]") {
    reset_format       reset;
    std::istringstream source("abcde");
    std::stringstream  compressed;
    REQUIRE(format::moduled_encode(source, compressed));
    auto const        encoded = compressed.str();
    std::stringstream dest;
    format::make_progress = false;
    CHECK_FALSE(format::moduled_decode(compressed, dest));
    format::make_progress = true;
    format::decode_ok     = false;
    std::istringstream input(encoded);
    CHECK_FALSE(format::moduled_decode(input, dest));
    format::encode_ok = false;
    std::istringstream failed_input("abc");
    CHECK_FALSE(format::moduled_encode(failed_input, dest));
    std::istringstream oversized(std::string(65536, 'x'));
    CHECK_FALSE(format::moduled_encode(oversized, dest));
    std::istringstream header(std::string("\0", 1));
    CHECK_FALSE(format::moduled_decode(header, dest));
    CHECK_THROWS_AS(format::moduled_decode(input, dest, 0), std::invalid_argument);
}
