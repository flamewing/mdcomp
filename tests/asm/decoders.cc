#include "fixtures/known_answers.hh"
#include "machine.hh"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string_view>

namespace {
    struct decoder {
        std::string_view name, codec;
        asm_test::cpu    architecture;
        bool             original, self_modifying;
        size_t           readable_padding;
    };

    std::vector<decoder> const decoders{
#include "decoder_images.inc"
    };

    asm_test::image image(decoder const& d) {
        return asm_test::load_image(
                std::filesystem::path(MDCOMP_ASM_IMAGES) / (std::string(d.name) + ".bin"),
                d.architecture, d.self_modifying);
    }

    bool compatible(decoder const& d, codec_test::fixture const& f) {
        if (d.codec != f.codec || !f.settings.size_header) {
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

    void check(decoder const& d, codec_test::fixture const& f, std::ostream& report, uint16_t art_base = 0) {
        asm_test::settings settings;
        settings.output_size      = f.expected.size();
        settings.art_base         = art_base;
        settings.readable_padding = d.readable_padding;
        settings.self_modifying   = d.self_modifying;
        auto const result         = asm_test::execute(image(d), f.compressed, settings);
        INFO(d.name << '/' << f.id << "\n" << result.diagnostic);
        CHECK((result.completed && result.bytes == f.expected));
        report << d.name << '\t' << f.id << '\t' << result.completed << '\t'
               << (result.bytes == f.expected) << '\t' << result.instructions << '\t'
               << result.cycles << '\t' << result.input_high_water << '\t'
               << f.compressed.size() << '\t' << settings.readable_padding << '\n';
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
    }
    // Published Enigma example with its documented nonzero starting art tile.
    for (auto const& d : decoders) if (d.codec == "enigma") {
        codec_test::fixture f;
        f.id = "enigma/published-starting-tile-1000";
        f.compressed = hex("070c00000010053d118fe0");
        f.expected = hex("100010011010101010101010501850175016501550145013501250115010");
        check(d, f, report, 0x1000);
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
                f.expected = codec_test::padded(*api, input);
                if (!compatible(d, f)) {
                    continue;
                }
                auto const encoded = codec_test::run(api->encode, input);
                REQUIRE(encoded.success);
                f.compressed = encoded.bytes;
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
        auto const result          = asm_test::execute(image(d), "", settings);
        INFO(d.name << result.diagnostic);
        CHECK_FALSE(result.completed);
        CHECK_FALSE(result.diagnostic.empty());
        report << d.name << "\tempty-truncated-payload\t" << !result.completed << '\t'
               << !result.diagnostic.empty() << '\t' << result.instructions << '\t'
               << result.cycles << '\t' << result.input_high_water << "\t0\t0\n";
        if (d.name == "sega-saxman-z80") {
            settings.output_size = 3;
            std::string const alias("\x03\0\0\xee\xf0", 5);
            auto const limited = asm_test::execute(image(d), alias, settings);
            CHECK_FALSE(limited.completed);
            CHECK(limited.diagnostic.find("unwritten") != std::string::npos);
            report << d.name << "\trebasing-only-alias\t" << !limited.completed << '\t'
                   << (limited.diagnostic.find("unwritten") != std::string::npos) << '\t'
                   << limited.instructions << '\t' << limited.cycles << '\t' << limited.input_high_water << "\t5\t0\n";
        }
    }
}
