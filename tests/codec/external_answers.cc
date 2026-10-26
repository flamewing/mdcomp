#include "fixtures/known_answers.hh"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace {
    std::string hex(std::string_view value) {
        std::string result;
        for (size_t i = 0; i < value.size(); i += 2) {
            result += static_cast<char>(
                    std::stoul(std::string(value.substr(i, 2)), nullptr, 16));
        }
        return result;
    }
}    // namespace

TEST_CASE(
        "Qualified external encodings decode to the original input",
        "[codec][external][known-answer]") {
    std::vector<codec_test::fixture> const fixtures{
#include "fixtures/external.inc"
    };
    REQUIRE_FALSE(fixtures.empty());
    for (auto const& fixture : fixtures) {
        DYNAMIC_SECTION(fixture.id) {
            auto const* api = static_cast<codec_test::api const*>(nullptr);
            for (auto const& codec : codec_test::codecs()) {
                if (codec.name == fixture.codec) {
                    api = &codec;
                    break;
                }
            }
            REQUIRE(api != nullptr);
            auto const result = codec_test::run(api->decode, fixture.compressed);
            CAPTURE(result.diagnostic);
            REQUIRE(result.success);
            CHECK(result.bytes == fixture.expected);
        }
    }
}
