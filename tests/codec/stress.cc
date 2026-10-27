#include "support/codec_api.hh"
#include "support/format_observer.hh"

#include <catch2/catch_test_macros.hpp>

namespace {
    uint32_t next(uint32_t& state) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
}    // namespace

TEST_CASE(
        "Seeded synthetic streams agree with independent wire observers",
        "[codec][stress][slow]") {
    for (uint32_t seed : {0x12345678U, 0x9e3779b9U, 0xdeadbeefU, 0x10101010U}) {
        for (auto const& api : codec_test::codecs()) {
            uint32_t state = seed;
            for (unsigned index = 0; index < 64; ++index) {
                size_t const   size       = (next(state) % 2049) / api.input_granularity
                                            * api.input_granularity;
                unsigned const pattern    = next(state) % 7;
                uint32_t const input_seed = next(state);
                auto const     input      = codec_test::corpus(size, pattern, input_seed);
                auto const     expected   = codec_test::padded(api, input);
                auto const     encoded    = codec_test::run(api.encode, input);
                auto decoded = encoded.success
                                       ? codec_test::run(api.decode, encoded.bytes)
                                       : codec_test::result{};
                codec_test::observation observed;
                std::string             observer_error;
                try {
                    if (encoded.success) {
                        observed = codec_test::observe(api.name, encoded.bytes);
                    }
                } catch (std::exception const& error) {
                    observer_error = error.what();
                }
                CAPTURE(std::string(api.name), seed, index, size, pattern, input_seed,
                        encoded.diagnostic, decoded.diagnostic, observer_error);
                if (!encoded.success || !decoded.success || decoded.bytes != expected
                    || !observer_error.empty() || observed.decoded != expected) {
                    decoded.diagnostic += "\nwire observer: " + observer_error;
                    codec_test::save_failure(
                            api.name,
                            "stress-" + std::to_string(seed) + "-"
                                    + std::to_string(index),
                            input, encoded, decoded, input_seed);
                }
                REQUIRE(encoded.success);
                REQUIRE(decoded.success);
                REQUIRE(observer_error.empty());
                CHECK((decoded.bytes == expected));
                CHECK((observed.decoded == expected));
            }
        }
    }
}
