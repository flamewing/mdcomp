#include "support/codec_api.hh"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

TEMPLATE_TEST_CASE(
        "Moduled codecs preserve data around module boundaries", "[codec][moduled]",
        comper, comperx, kosinski, kosplus, lzkn1, rocket, saxman, snkrle) {
    auto const& codec = codec_test::for_codec<TestType>();
    REQUIRE(codec.moduled_encode != nullptr);
    REQUIRE(codec.moduled_decode != nullptr);
    auto const grain = codec.input_granularity;
    for (size_t size :
         {size_t{0}, grain, codec.module_size - grain, codec.module_size,
          codec.module_size + grain, 2 * codec.module_size + grain}) {
        for (unsigned pattern : {1U, 4U, 5U, 6U}) {
            for (size_t padding : {codec.module_padding, size_t{32}}) {
                codec_test::options settings;
                settings.module_padding = padding;
                auto const input        = codec_test::corpus(size, pattern);
                auto const encoded
                        = codec_test::run(codec.moduled_encode, input, settings);
                auto const decoded = encoded.success ? codec_test::run(
                                                               codec.moduled_decode,
                                                               encoded.bytes, settings)
                                                     : codec_test::result{};
                CAPTURE(std::string(codec.name), size, pattern, padding, encoded.diagnostic,
                        decoded.diagnostic);
                if (!encoded.success || !decoded.success || decoded.bytes != input) {
                    codec_test::save_failure(
                            codec.name,
                            "moduled-" + std::to_string(size) + "-"
                                    + std::to_string(pattern) + "-"
                                    + std::to_string(padding),
                            input, encoded, decoded, 0x12345678);
                }
                REQUIRE(encoded.success);
                REQUIRE(encoded.bytes.size() >= 2);
                auto const declared = (static_cast<unsigned char>(encoded.bytes[0]) << 8)
                                      | static_cast<unsigned char>(encoded.bytes[1]);
                CHECK(static_cast<size_t>(declared) == input.size());
                REQUIRE(decoded.success);
                CHECK(decoded.diagnostic.empty());
                CHECK(decoded.bytes == input);
            }
        }
    }
}

TEMPLATE_TEST_CASE(
        "Moduled codecs reject incomplete or unrepresentable frame sizes",
        "[codec][moduled][negative]", comper, comperx, kosinski, kosplus, lzkn1, rocket,
        saxman, snkrle) {
    auto const& codec = codec_test::for_codec<TestType>();
    for (auto const& input : {std::string{}, std::string(1, '\0')}) {
        auto const decoded = codec_test::run(codec.moduled_decode, input);
        CHECK_FALSE(decoded.success);
        CHECK(decoded.bytes.empty());
    }
    auto const encoded = codec_test::run(codec.moduled_encode, std::string(65536, 'x'));
    CHECK_FALSE(encoded.success);
    CHECK(encoded.bytes.empty());
    codec_test::options settings;
    settings.module_padding = 0;
    auto const invalid_encode = codec_test::run(codec.moduled_encode, "xx", settings);
    CHECK_FALSE(invalid_encode.success);
    CHECK(invalid_encode.bytes.empty());
    CHECK_FALSE(invalid_encode.diagnostic.empty());
    auto const valid = codec_test::run(codec.moduled_encode, "xx");
    REQUIRE(valid.success);
    auto const invalid_decode = codec_test::run(codec.moduled_decode, valid.bytes, settings);
    CHECK_FALSE(invalid_decode.success);
    CHECK(invalid_decode.bytes.empty());
    CHECK_FALSE(invalid_decode.diagnostic.empty());
}

TEMPLATE_TEST_CASE(
        "Moduled headers preserve the largest legal input size", "[codec][moduled][slow]",
        comper, comperx, kosinski, kosplus, lzkn1, rocket, saxman, snkrle) {
    auto const&  codec   = codec_test::for_codec<TestType>();
    size_t const size    = 65535 - 65535 % codec.input_granularity;
    auto const   input   = codec_test::corpus(size, 1);
    auto const   encoded = codec_test::run(codec.moduled_encode, input);
    CAPTURE(std::string(codec.name), size, encoded.diagnostic);
    REQUIRE(encoded.success);
    auto const decoded = codec_test::run(codec.moduled_decode, encoded.bytes);
    CAPTURE(decoded.diagnostic);
    REQUIRE(decoded.success);
    CHECK(decoded.bytes == input);
}
