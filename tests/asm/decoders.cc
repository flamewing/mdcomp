#include "decoder_registry.hh"
#include "fixtures/known_answers.hh"
#include "machine.hh"
#include "support/wire_builder.hh"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string_view>

namespace {
    using asm_test::decoder;
    auto const decoders = asm_test::decoder_images();

    asm_test::image image(decoder const& d) {
        return asm_test::load_image(
                std::filesystem::path(MDCOMP_ASM_IMAGES) / (std::string(d.name) + ".bin"),
                d.architecture, d.self_modifying);
    }

    bool compatible(decoder const& d, codec_test::fixture const& f) {
        if (d.codec != f.codec || (!d.headerless && !f.settings.size_header)) {
            return false;
        }
        if (d.codec == "snkrle" && (f.expected.empty() || f.expected.size() % 2)) {
            return false;
        }
        if (d.codec == "nemesis" && f.expected.empty()) {
            return false;
        }
        if (d.name == "sega-saxman-z80") {
            if (f.expected.size() > 0x800 || f.id == "saxman/seed-3"
                || f.id == "saxman/seed-18") {
                return false;
            }
        }
        return true;
    }

    void check(
            decoder const& d, codec_test::fixture const& supplied, std::ostream& report,
            uint16_t art_base = 0) {
        auto f = supplied;
        if (d.headerless && f.settings.size_header) {
            REQUIRE(f.compressed.size() >= 2);
            f.settings.compressed_size
                    = static_cast<unsigned char>(f.compressed[0])
                      | (size_t(static_cast<unsigned char>(f.compressed[1])) << 8);
            f.settings.size_header = false;
            f.compressed.erase(0, 2);
        }
        asm_test::settings settings;
        settings.output_size         = f.expected.size();
        settings.art_base            = art_base;
        settings.readable_padding    = d.readable_padding;
        settings.self_modifying      = d.self_modifying;
        settings.stack_scratch_bytes = d.stack_scratch_bytes;
        if (d.headerless) {
            REQUIRE(f.settings.compressed_size <= 0xffff);
            settings.body_size = static_cast<uint16_t>(f.settings.compressed_size);
        }
        auto const result = asm_test::execute(image(d), f.compressed, settings);
        INFO(d.name << '/' << f.id << "\n" << result.diagnostic);
        INFO(asm_test::timing(d.architecture, result));
        CHECK((result.completed && result.bytes == f.expected));
        if (d.headerless) {
            CHECK(result.input_high_water <= *settings.body_size);
        }
        if (d.stack_scratch_bytes) {
            // Saxman's byte-swapping header discards one scratch stack byte.
            // A second poison pattern must not affect the decompressed data.
            settings.stack_scratch_fill = 0x5a;
            auto const changed = asm_test::execute(image(d), f.compressed, settings);
            CHECK(changed.completed);
            CHECK(changed.bytes == result.bytes);
            CHECK(changed.cycles == result.cycles);
        }
        report << d.name << '\t' << f.id << '\t' << result.completed << '\t'
               << (result.bytes == f.expected) << '\t' << result.instructions << '\t'
               << result.cycles << '\t' << result.input_high_water << '\t'
               << f.compressed.size() << '\t' << settings.readable_padding << '\t'
               << result.bus_reads << '\t' << result.bus_writes << '\t'
               << asm_test::timing(d.architecture, result) << '\n';
        if (!result.completed || result.bytes != f.expected) {
            auto path = std::filesystem::path("failures/asm") / d.name / f.id;
            std::filesystem::create_directories(path);
            std::ofstream(path / "input.bin", std::ios::binary)
                    .write(f.compressed.data(),
                           static_cast<std::streamsize>(f.compressed.size()));
            std::ofstream(path / "expected.bin", std::ios::binary)
                    .write(f.expected.data(),
                           static_cast<std::streamsize>(f.expected.size()));
            std::ofstream(path / "actual.bin", std::ios::binary)
                    .write(result.bytes.data(),
                           static_cast<std::streamsize>(result.bytes.size()));
            std::ofstream(path / "machine.txt") << result.diagnostic;
        }
    }

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
        "Assembly decoders bootstrap against hand-constructed streams",
        "[asm][bootstrap]") {
    std::ofstream report("bootstrap.tsv");
    for (auto const& d : decoders) {
        size_t count = 0;
        for (auto const& f : codec_test::known_answers()) {
            if (compatible(d, f)) {
                ++count;
                check(d, f, report);
            }
        }
        INFO(d.name);
        REQUIRE(count > 0);
        if (d.headerless) {
            codec_test::fixture empty;
            empty.id                   = "saxman/explicit-zero-body";
            empty.codec                = "saxman";
            empty.settings.size_header = false;
            empty.compressed = "TRAIL";    // Zero body size must leave this unread.
            check(d, empty, report);
        }
    }
    // Published Enigma example with its documented nonzero starting art tile.
    for (auto const& d : decoders) {
        if (d.codec == "enigma") {
            codec_test::fixture f;
            f.id         = "enigma/published-starting-tile-1000";
            f.compressed = hex("070c00000010053d118fe0");
            f.expected
                    = hex("100010011010101010101010501850175016501550145013501250115010");
            check(d, f, report, 0x1000);
        }
    }
}

TEST_CASE("Assembly decoders accept qualified external encodings", "[asm][external]") {
    std::vector<codec_test::fixture> const fixtures{
#include "fixtures/external.inc"
    };
    std::ofstream report("external.tsv");
    for (auto const& d : decoders) {
        for (auto const& f : fixtures) {
            if (compatible(d, f)) {
                check(d, f, report);
            }
        }
    }
}

TEST_CASE(
        "Assembly optimization configurations preserve every small match tail",
        "[asm][bootstrap][optimization]") {
    using codec_test::fixture;
    using codec_test::wire::descriptors;
    std::vector<fixture> fixtures;
    // Every tail modulo 2, 4, and 8, plus larger loop/counter boundaries.
    std::vector<unsigned> lengths;
    for (unsigned n = 2; n <= 17; ++n) {
        lengths.push_back(n);
    }
    for (unsigned n : {63U, 64U, 65U, 127U, 128U, 129U, 254U, 255U, 256U}) {
        lengths.push_back(n);
    }
    for (bool x : {false, true}) {
        for (unsigned n : lengths) {
            if (x && n == 256) {
                continue;
            }
            for (unsigned distance = 1; distance <= (x ? 2U : 1U); ++distance) {
                descriptors wire(16, false, false, false);
                std::string expected;
                for (unsigned i = 0; i < distance; ++i) {
                    wire.bit(0);
                    wire.put_word(i ? 0x1234 : 0xabcd);
                    codec_test::wire::word(expected, i ? 0x1234 : 0xabcd);
                }
                wire.bit(1);
                wire.put(x && distance == 1 ? 0 : 0x100 - distance + unsigned(x));
                wire.put(x ? (0x80 - (n >> 1)) | ((n & 1) << 7) : n - 1);
                for (unsigned i = 0; i < n; ++i) {
                    codec_test::wire::word(
                            expected, distance == 2 && (i & 1) ? 0x1234 : 0xabcd);
                }
                wire.bit(1);
                // Exercise both ComperX exit slots (RLE and ordinary copy).
                wire.put(x && distance == 1 ? 0 : 0xff);
                wire.put(0);
                fixtures.push_back({
                        "optimization/" + std::string(x ? "comperx" : "comper") + "-"
                                + std::to_string(distance) + "-" + std::to_string(n),
                        x ? "comperx" : "comper",
                        wire.data,
                        expected,
                        {"unroll-tail", "overlap", "terminator"},
                        {},
                        {}
                });
            }
        }
    }
    for (bool plus : {false, true}) {
        for (unsigned kind = 0; kind < 3; ++kind) {
            std::vector<unsigned> counts;
            if (kind == 0) {
                counts = {2, 3, 4, 5};
            }
            if (kind == 1) {
                counts = {3, 4, 5, 6, 7, 8, 9};
            }
            if (kind == 2) {
                for (unsigned n = plus ? 10U : 3U; n <= (plus ? 26U : 19U); ++n) {
                    counts.push_back(n);
                }
                counts.insert(counts.end(), {63, 64, 65, 127, 128, 129, 254, 255, 256});
                if (plus) {
                    counts.insert(counts.end(), {263, 264});
                }
            }
            for (unsigned n : counts) {
                descriptors wire(plus ? 8 : 16, !plus, true, !plus);
                wire.bit(1);
                wire.put('A');
                wire.bits(kind == 0 ? 0 : 1, 2);
                if (kind == 0) {
                    if (plus) {
                        wire.put(0xff);
                    }
                    wire.bits(n - 2, 2);
                    if (!plus) {
                        wire.put(0xff);
                    }
                } else {
                    auto const high
                            = 0xf8U | (kind == 1 ? (plus ? 10U - n : n - 2U) : 0U);
                    wire.put(plus ? high : 0xff);
                    wire.put(plus ? 0xff : high);
                    if (kind == 2) {
                        wire.put(plus ? n - 9 : n - 1);
                    }
                }
                wire.bits(1, 2);
                wire.put(0);
                wire.put(0);
                wire.put(0);
                fixtures.push_back({
                        "optimization/" + std::string(plus ? "kosplus" : "kosinski") + "-"
                                + std::to_string(kind) + "-" + std::to_string(n),
                        plus ? "kosplus" : "kosinski",
                        wire.data,
                        std::string(n + 1, 'A'),
                        {"unroll-tail", "overlap", "terminator"},
                        {},
                        {}
                });
            }
        }
    }
    for (bool sax : {false, true}) {
        for (unsigned n = sax ? 3U : 2U; n <= (sax ? 18U : 64U); ++n) {
            for (bool seed : {false, true}) {
                descriptors wire(8, true, true, false);
                if (!seed) {
                    wire.bit(1);
                    wire.put('A');
                }
                wire.bit(0);
                if (sax) {
                    wire.put(0xee);
                    wire.put(0xf0 | (n - 3));
                } else {
                    auto const offset = seed ? 0U : 0x3c0U;
                    wire.put(((n - 1) << 2) | (offset >> 8));
                    wire.put(offset);
                }
                std::string expected(
                        n + unsigned(!seed), seed ? (sax ? '\0' : ' ') : 'A');
                std::string header;
                if (sax) {
                    codec_test::wire::byte(header, unsigned(wire.data.size()));
                    codec_test::wire::byte(header, 0);
                } else {
                    codec_test::wire::word(header, unsigned(expected.size()));
                    codec_test::wire::word(header, unsigned(wire.data.size()));
                }
                fixtures.push_back({
                        "optimization/" + std::string(sax ? "saxman" : "rocket") + "-"
                                + std::to_string(unsigned(seed)) + "-"
                                + std::to_string(n),
                        sax ? "saxman" : "rocket",
                        header + wire.data,
                        expected,
                        {"unroll-tail", seed ? "seeded-fill" : "overlap"},
                        {},
                        {}
                });
            }
        }
    }
    std::ofstream report("optimization.tsv");
    for (auto const& f : fixtures) {
        // This is corroboration, not the source of the expected bytes above.
        for (auto const& api : codec_test::codecs()) {
            if (api.name == f.codec) {
                auto const decoded = codec_test::run(api.decode, f.compressed);
                INFO(f.id);
                REQUIRE(decoded.success);
                REQUIRE(decoded.bytes == f.expected);
            }
        }
        for (auto const& d : decoders) {
            // These target the shipped optimization implementations; Sega's
            // original Saxman aliases have separately qualified semantics.
            if (!d.original && compatible(d, f)) {
                check(d, f, report);
            }
        }
    }
}

TEST_CASE("C++ encoders interoperate with assembly decoders", "[asm][encode]") {
    std::ofstream report("encode.tsv");
    for (auto const& d : decoders) {
        auto const* api = static_cast<codec_test::api const*>(nullptr);
        for (auto const& codec : codec_test::codecs()) {
            if (codec.name == d.codec) {
                api = &codec;
            }
        }
        REQUIRE(api != nullptr);
        for (size_t size :
             {size_t{0}, api->input_granularity, size_t{128}, size_t{512},
              size_t{4096}}) {
            if (d.codec == "nemesis") {
                size = (size + 31) / 32 * 32;
            }
            for (unsigned pattern : {0U, 1U, 2U, 4U}) {
                if (!size && pattern != 0) {
                    continue;
                }
                auto const          input = codec_test::corpus(size, pattern);
                codec_test::fixture f;
                f.id    = "cpp-" + std::to_string(size) + "-" + std::to_string(pattern);
                f.codec = d.codec;
                f.settings.size_header = !d.headerless;
                f.expected             = codec_test::padded(*api, input);
                if (!compatible(d, f)) {
                    continue;
                }
                auto const encoded = codec_test::run(api->encode, input, f.settings);
                REQUIRE(encoded.success);
                f.compressed = encoded.bytes;
                if (d.headerless) {
                    f.settings.compressed_size = f.compressed.size();
                }
                check(d, f, report);
            }
        }
    }
}

TEST_CASE(
        "Rocket assembly accepts unsigned compressed lengths above 32767",
        "[asm][encode]") {
    auto const& api     = codec_test::for_codec<rocket>();
    auto const  input   = codec_test::corpus(32768, 4);
    auto const  encoded = codec_test::run(api.encode, input);
    REQUIRE(encoded.success);
    REQUIRE(encoded.bytes.size() > 32771);
    REQUIRE((static_cast<unsigned char>(encoded.bytes[2]) & 0x80) != 0);
    std::ofstream report("rocket-large.tsv");
    for (auto const& d : decoders) {
        if (d.codec == "rocket") {
            codec_test::fixture fixture;
            fixture.id         = "rocket/unsigned-payload-length";
            fixture.compressed = encoded.bytes;
            fixture.expected   = input;
            check(d, fixture, report);
        }
    }
}

TEST_CASE(
        "Assembly malformed streams cannot escape memory or execution limits",
        "[asm][negative]") {
    std::ofstream report("negative.tsv");
    for (auto const& d : decoders) {
        // These are not assertions that the original routine validates input.
        // They verify containment when a routine reads past a truncated stream.
        asm_test::settings settings;
        settings.output_size       = 16;
        settings.readable_padding  = 0;
        settings.instruction_limit = 10000;
        settings.self_modifying    = d.self_modifying;
        if (d.headerless) {
            settings.body_size = 1;
        }
        auto const result = asm_test::execute(image(d), "", settings);
        INFO(d.name << result.diagnostic);
        CHECK_FALSE(result.completed);
        CHECK_FALSE(result.diagnostic.empty());
        report << d.name << "\tempty-truncated-payload\t" << !result.completed << '\t'
               << !result.diagnostic.empty() << '\t' << result.instructions << '\t'
               << result.cycles << '\t' << result.input_high_water << "\t0\t0\t"
               << result.bus_reads << '\t' << result.bus_writes << '\t'
               << asm_test::timing(d.architecture, result) << '\n';
        if (d.name == "sega-saxman-z80") {
            settings.output_size = 3;
            std::string const alias("\x03\0\0\xee\xf0", 5);
            auto const        limited = asm_test::execute(image(d), alias, settings);
            CHECK_FALSE(limited.completed);
            CHECK(limited.diagnostic.find("unwritten") != std::string::npos);
            report << d.name << "\trebasing-only-alias\t" << !limited.completed << '\t'
                   << (limited.diagnostic.find("unwritten") != std::string::npos) << '\t'
                   << limited.instructions << '\t' << limited.cycles << '\t'
                   << limited.input_high_water << "\t5\t0\t" << limited.bus_reads << '\t'
                   << limited.bus_writes << '\t'
                   << asm_test::timing(d.architecture, limited) << '\n';
        }
    }
}
