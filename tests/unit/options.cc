#include "mdcomp/options_lib.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

namespace {
    class capture {
        std::ostream&   stream;
        std::streambuf* original;

    public:
        std::ostringstream text;

        explicit capture(std::ostream& output) : stream(output), original(nullptr) {
            original = output.rdbuf(text.rdbuf());
        }

        ~capture() {
            stream.rdbuf(original);
        }
    };

    struct format {
        static bool encode(std::istream&, std::ostream&) {
            return true;
        }

        static bool decode(std::istream&, std::ostream&) {
            return true;
        }
    };

    struct options {
        using format_t = format;
        bool print_end = false;
    };
}    // namespace

TEST_CASE(
        "Option integers accept signed minima and reject invalid magnitudes",
        "[options]") {
    capture errors(std::cerr);
    int64_t signed_value = 42;
    optionlib::read_value("-9223372036854775808", signed_value);
    CHECK(signed_value == std::numeric_limits<int64_t>::min());
    optionlib::read_value("-0x8000000000000000", signed_value);
    CHECK(signed_value == std::numeric_limits<int64_t>::min());
    int8_t small = 0;
    optionlib::read_value("-128", small);
    CHECK(small == -128);
    for (auto text : {"128", "-129", "", "-", "0x", "12garbage"}) {
        small = 7;
        CHECK_THROWS_AS(optionlib::read_value(text, small), int);
        CHECK(small == 7);
    }
    uint64_t unsigned_value{};
    optionlib::read_value("0xffffffffffffffff", unsigned_value);
    CHECK(unsigned_value == std::numeric_limits<uint64_t>::max());
    CHECK_THROWS_AS(optionlib::read_value("-1", unsigned_value), int);
}

TEST_CASE("Decode position is printed only when requested", "[options]") {
    capture            output(std::cout);
    std::istringstream input("abc");
    std::ostringstream dest;
    options            settings;
    optionlib::do_decode(input, dest, settings);
    CHECK(output.text.str().empty());
    settings.print_end = true;
    optionlib::do_decode(input, dest, settings);
    CHECK(output.text.str() == "0x000000");
}

TEST_CASE("CLI codec dispatch propagates explicit failure results", "[options]") {
    capture errors(std::cerr);
    CHECK_THROWS_AS(
            optionlib::invoke_codec(
                    [] {
                        return false;
                    },
                    std::tuple{}),
            int);
    CHECK_NOTHROW(
            optionlib::invoke_codec(
                    [] {
                        return true;
                    },
                    std::tuple{}));
    CHECK_NOTHROW(optionlib::invoke_codec([] {}, std::tuple{}));
}

TEST_CASE("CLI file helpers reject destructive input-output aliases", "[options]") {
    capture    errors(std::cerr);
    auto const file
            = std::filesystem::current_path() / "mdcomp-options-same-file-test.bin";

    struct cleanup {
        std::filesystem::path path;

        ~cleanup() {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } remove_file{file};

    {
        std::ofstream output(file, std::ios::binary);
        output << "preserve me";
    }
    options settings;
    CHECK(optionlib::encode_file(file, file, settings) == 3);
    CHECK(optionlib::decode_file(file, file, settings) == 3);
    std::ifstream     input(file, std::ios::binary);
    std::string const contents(
            (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>{});
    CHECK(contents == "preserve me");
}
