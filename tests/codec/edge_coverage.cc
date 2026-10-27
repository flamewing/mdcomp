#include "fixtures/known_answers.hh"
#include "support/format_observer.hh"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <map>
#include <vector>

namespace {
    struct obligation {
        std::string_view codec, mode, edge;
    };

    constexpr obligation obligations[]{
#include "edge_obligations.inc"
    };
    using coverage = std::map<std::string, std::map<std::string, std::string>>;

    void record(
            coverage& covered, std::string_view codec, std::string const& name,
            codec_test::observation const& result) {
        for (auto const& edge : result.edges) {
            covered[std::string(codec)].try_emplace(edge, name);
        }
    }

    void check_obligations(coverage const& covered, std::string_view mode) {
        for (auto const& required : obligations) {
            if (required.mode != mode) {
                continue;
            }
            CAPTURE(std::string(required.codec), std::string(required.mode),
                    std::string(required.edge));
            auto const found = covered.find(std::string(required.codec));
            REQUIRE(found != covered.end());
            CHECK(found->second.count(std::string(required.edge)) == 1);
        }
    }

    void report(coverage const& covered, char const* filename) {
        std::ofstream out(filename);
        REQUIRE(out.good());
        out << "codec\tedge\tfirst_case\n";
        for (auto const& [codec, edges] : covered) {
            for (auto const& [edge, first] : edges) {
                out << codec << '\t' << edge << '\t' << first << '\n';
            }
        }
        REQUIRE(out.good());
    }

    std::vector<std::pair<std::string, std::string>> recipes() {
        std::vector<std::pair<std::string, std::string>> inputs;
        for (size_t size :
             {size_t(0), size_t(2), size_t(4), size_t(8), size_t(16), size_t(32),
              size_t(72), size_t(258), size_t(1024)}) {
            for (unsigned pattern = 0; pattern < 6; ++pattern) {
                inputs.emplace_back(
                        "corpus-" + std::to_string(size) + "-" + std::to_string(pattern),
                        codec_test::corpus(size, pattern));
            }
        }
        auto random = codec_test::corpus(258, 4);
        for (size_t length : {size_t(3), size_t(5), size_t(9), size_t(10)}) {
            inputs.emplace_back(
                    "distant-match-" + std::to_string(length),
                    random + random.substr(0, length) + "\x77");
        }
        for (size_t length = 2; length <= 9; ++length) {
            inputs.emplace_back(
                    "short-run-" + std::to_string(length),
                    std::string(length, 'A') + "B");
        }
        std::string tiles;
        auto        tile = [&](unsigned value) {
            tiles += char(value >> 8);
            tiles += char(value & 255);
        };
        for (unsigned i = 0; i < 32; ++i) {
            tile(0x100);
        }
        for (unsigned i = 0; i < 17; ++i) {
            tile(0x321);
        }
        for (unsigned i = 0; i < 32; ++i) {
            tile(0x500 + i);
        }
        for (unsigned i = 0; i < 16; ++i) {
            tile(0x200 + i);
        }
        for (unsigned i = 0; i < 32; ++i) {
            tile(0x700 - i);
        }
        for (unsigned i = 0; i < 16; ++i) {
            tile(0x900 + 13 * i);
        }
        inputs.emplace_back("tile-packet-choices", tiles);
        return inputs;
    }
}    // namespace

TEST_CASE(
        "Independent wire observers verify decoder forms and semantic boundaries",
        "[codec][edges][known-answer]") {
    coverage covered;
    for (auto const& fixture : codec_test::known_answers()) {
        CAPTURE(fixture.id);
        auto const result = codec_test::observe(
                fixture.codec, fixture.compressed, fixture.settings);
        REQUIRE((result.decoded == fixture.expected));
        record(covered, fixture.codec, fixture.id, result);
    }
    report(covered, "decoder-edges.tsv");
    check_obligations(covered, "decoder");
    check_obligations(covered, "boundary");
}

TEST_CASE("Encoder wire choices cover declared forms", "[codec][edges][encoder]") {
    coverage covered;
    for (auto const& api : codec_test::codecs()) {
        for (auto const& [name, input] : recipes()) {
            if (input.size() % api.input_granularity) {
                continue;
            }
            for (bool header : {true, false}) {
                if (!header && api.name != "saxman") {
                    continue;
                }
                codec_test::options settings;
                settings.size_header = header;
                auto const encoded   = codec_test::run(api.encode, input, settings);
                CAPTURE(std::string(api.name), name, header, encoded.diagnostic);
                REQUIRE(encoded.success);
                settings.compressed_size = encoded.bytes.size();
                if (!header) {
                    // basic_decoder aligns the file to an even byte count;
                    // Saxman's declared body excludes that transport padding.
                    auto const framed = codec_test::run(api.encode, input);
                    REQUIRE(framed.success);
                    REQUIRE(framed.bytes.size() >= 2);
                    REQUIRE((encoded.bytes == framed.bytes.substr(2)));
                    settings.compressed_size
                            = static_cast<unsigned char>(framed.bytes[0])
                              | (size_t(static_cast<unsigned char>(framed.bytes[1]))
                                 << 8);
                }
                INFO("encoded size=" << encoded.bytes.size());
                std::string hex;
                for (unsigned char value : encoded.bytes) {
                    hex += "0123456789abcdef"[value >> 4];
                    hex += "0123456789abcdef"[value & 15];
                }
                INFO("wire=" << hex);
                auto const result
                        = codec_test::observe(api.name, encoded.bytes, settings);
                REQUIRE((result.decoded == codec_test::padded(api, input)));
                record(covered, api.name, name + (header ? "" : "-headerless"), result);
            }
        }
    }
    report(covered, "encoder-edges.tsv");
    check_obligations(covered, "encoder");
}

TEST_CASE(
        "Wire observers reject truncated control data and impossible history",
        "[codec][edges][negative]") {
    for (auto const& api : codec_test::codecs()) {
        CAPTURE(std::string(api.name));
        CHECK_THROWS(codec_test::observe(api.name, ""));
    }
    CHECK_THROWS(codec_test::observe("comper", std::string("\x80\0\xff\x01", 4)));
    CHECK_THROWS(codec_test::observe("saxman", std::string("\x03\0\0\0", 4)));
    CHECK_THROWS(
            codec_test::observe("nemesis", std::string("\0\x01\x81\x11\0\x11\0\xff", 8)));
}
