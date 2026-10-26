#include "support/codec_api.hh"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

namespace {
    constexpr uint32_t seed = 0x12345678;

    void round_trip(codec_test::api const& codec, size_t size, unsigned pattern) {
        auto const input    = codec_test::corpus(size, pattern, seed);
        auto const encoded  = codec_test::run(codec.encode, input);
        auto const decoded  = encoded.success
                                      ? codec_test::run(codec.decode, encoded.bytes)
                                      : codec_test::result{};
        auto const expected = codec_test::padded(codec, input);
        CAPTURE(std::string(codec.name), size, pattern, seed, encoded.diagnostic, decoded.diagnostic);
        if (!encoded.success || !decoded.success || decoded.bytes != expected) {
            codec_test::save_failure(
                    codec.name,
                    "basic-" + std::to_string(size) + "-" + std::to_string(pattern),
                    input, encoded, decoded, seed);
        }
        REQUIRE(encoded.success);
        REQUIRE(decoded.success);
        CHECK(encoded.diagnostic.empty());
        CHECK(decoded.diagnostic.empty());
        CHECK(decoded.bytes == expected);
        CHECK(encoded.bytes.size() <= 2 * input.size() + 1024);
        auto const repeated = codec_test::run(codec.encode, input);
        REQUIRE(repeated.success);
        CHECK(repeated.bytes == encoded.bytes);
    }
}    // namespace

TEMPLATE_TEST_CASE(
        "Codec contracts round-trip legal small and descriptor-boundary inputs",
        "[codec][contract]", comper, comperx, enigma, kosinski, kosplus, lzkn1, nemesis,
        rocket, saxman, snkrle) {
    auto const& codec = codec_test::for_codec<TestType>();
    for (size_t size :
         {size_t{0}, size_t{2}, size_t{4}, size_t{14}, size_t{16}, size_t{18}, size_t{30},
          size_t{32}, size_t{34}, size_t{62}, size_t{64}, size_t{66}, size_t{254},
          size_t{256}, size_t{258}, size_t{512}}) {
        for (unsigned pattern = 0; pattern < 6; ++pattern) {
            round_trip(codec, size, pattern);
        }
    }
}

TEMPLATE_TEST_CASE(
        "Byte codecs round-trip odd lengths and explicit padding policies",
        "[codec][padding]", comper, comperx, kosinski, kosplus, lzkn1, nemesis, rocket,
        saxman, snkrle) {
    auto const& codec = codec_test::for_codec<TestType>();
    for (size_t size :
         {size_t{1}, size_t{3}, size_t{15}, size_t{31}, size_t{33}, size_t{257}}) {
        round_trip(codec, size, 4);
    }
}

TEST_CASE(
        "Saxman exposes sized and externally sized streams", "[codec][options][saxman]") {
    auto const& codec = codec_test::for_codec<saxman>();
    for (size_t size : {size_t{1}, size_t{16}, size_t{255}, size_t{4096}}) {
        auto const input = codec_test::corpus(size, 4);
        for (bool header : {false, true}) {
            codec_test::options settings;
            settings.size_header = header;
            auto       encoded   = codec_test::run(codec.encode, input, settings);
            auto const sized     = codec_test::run(codec.encode, input);
            REQUIRE(sized.success);
            REQUIRE(sized.bytes.size() >= 2);
            settings.compressed_size
                    = static_cast<unsigned char>(sized.bytes[0])
                      | (static_cast<size_t>(static_cast<unsigned char>(sized.bytes[1]))
                         << 8);
            if (!header) {
                CHECK(encoded.bytes == sized.bytes.substr(2));
            }
            auto decoded = codec_test::run(codec.decode, encoded.bytes, settings);
            CAPTURE(size, header, encoded.diagnostic, decoded.diagnostic);
            REQUIRE(encoded.success);
            REQUIRE(decoded.success);
            CHECK(decoded.bytes == input);
        }
    }
}
