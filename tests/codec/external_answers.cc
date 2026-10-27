#include "fixtures/external_answers.hh"

#include "support/format_observer.hh"
#include "support/wire_builder.hh"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

TEST_CASE(
        "Qualified external encodings decode to the original input",
        "[codec][external][known-answer]") {
    auto const& fixtures = codec_test::external_answers();
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
            auto const observed = codec_test::observe(fixture.codec, fixture.compressed);
            CHECK((observed.decoded == fixture.expected));
        }
    }
}

TEST_CASE(
        "Qualified external bodies decode as short final modules",
        "[codec][external][moduled]") {
    size_t count = 0;
    for (auto const& fixture : codec_test::external_answers()) {
        for (auto const& api : codec_test::codecs()) {
            if (api.name != fixture.codec || !api.moduled_decode) {
                continue;
            }
            CAPTURE(fixture.id);
            REQUIRE(fixture.expected.size() <= api.module_size);
            std::string framed;
            codec_test::wire::word(
                    framed, static_cast<unsigned>(fixture.expected.size()));
            framed += fixture.compressed;
            // The external encoding is retained byte-for-byte. Only the
            // test-owned outer module size is added; lineage stays external.
            for (size_t padding : {size_t(1), size_t(3), size_t(16), size_t(32)}) {
                codec_test::options settings;
                settings.module_padding = padding;
                auto const result       = codec_test::run(
                        api.moduled_decode, framed, settings, 0, "prefix");
                CAPTURE(padding, result.diagnostic);
                REQUIRE(result.success);
                CHECK((result.bytes == "prefix" + fixture.expected));
                ++count;
            }
        }
    }
    CHECK(count > 0);
}
