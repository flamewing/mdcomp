#include "fixtures/known_answers.hh"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>

TEMPLATE_TEST_CASE(
        "Hand-constructed codec streams decode to independent expected bytes",
        "[codec][known-answer]", comper, comperx, enigma, kosinski, kosplus, lzkn1,
        nemesis, rocket, saxman, snkrle) {
    auto const& codec  = codec_test::for_codec<TestType>();
    size_t      tested = 0;
    for (auto const& fixture : codec_test::known_answers()) {
        if (fixture.codec != codec.name) {
            continue;
        }
        ++tested;
        DYNAMIC_SECTION(fixture.id) {
            auto const decoded
                    = codec_test::run(codec.decode, fixture.compressed, fixture.settings);
            CAPTURE(decoded.diagnostic);
            REQUIRE(decoded.success);
            CHECK(decoded.diagnostic.empty());
            CHECK(decoded.bytes == fixture.expected);
            if (fixture.consumed) {
                CHECK(decoded.consumed == fixture.consumed);
            }

            auto const embedded = codec_test::run(
                    codec.decode, "PREFIX" + fixture.compressed + "TRAILER",
                    fixture.settings, 6);
            REQUIRE(embedded.success);
            CHECK(embedded.bytes == fixture.expected);
            if (fixture.consumed) {
                CHECK(embedded.consumed == fixture.consumed);
            }
            auto const appended = codec_test::run(
                    codec.decode, fixture.compressed, fixture.settings, 0, "EXISTING");
            REQUIRE(appended.success);
            CHECK(appended.bytes == "EXISTING" + fixture.expected);
        }
    }
    REQUIRE(tested != 0);
}

TEST_CASE(
        "Known-answer fixture identifiers are unique and have edge annotations",
        "[codec][fixtures]") {
    std::set<std::string> ids;
    for (auto const& fixture : codec_test::known_answers()) {
        CAPTURE(fixture.id);
        CHECK(ids.insert(fixture.id).second);
        CHECK_FALSE(fixture.edges.empty());
    }
}
