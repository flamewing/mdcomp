#include "decoder_registry.hh"
#include "fixtures/known_answers.hh"
#include "support/wire_builder.hh"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace {
    struct archive {
        std::string compressed, expected;
        uint16_t    destination = 0x1200;
    };

    std::vector<uint8_t> expected_vram(std::vector<archive> const& archives) {
        std::vector<uint8_t> expected(0x10000, 0xa5);
        for (auto const& archive : archives) {
            for (size_t i = 0; i < archive.expected.size(); ++i) {
                expected[(archive.destination + i) & 0xffff]
                        = static_cast<uint8_t>(archive.expected[i]);
            }
        }
        return expected;
    }

    void longword(std::string& data, uint32_t value) {
        codec_test::wire::word(data, value >> 16);
        codec_test::wire::word(data, value);
    }

    asm_test::result run(
            std::string_view codec, unsigned words, std::vector<archive> const& archives,
            asm_test::module_configuration config = {}, unsigned flags = 0,
            bool expect_complete = true) {
        auto const name = "module-" + std::string(codec) + "-dma" + std::to_string(words);
        auto const image = asm_test::load_image(
                asm_test::image_directory() / (name + ".bin"), asm_test::cpu::m68000);
        std::string payload, bodies;
        codec_test::wire::word(payload, static_cast<unsigned>(archives.size()));
        codec_test::wire::word(payload, flags);
        auto const header_size = 4 + 6 * archives.size();
        for (auto const& archive : archives) {
            if (bodies.size() & 1) {
                bodies += '\0';
            }
            longword(
                    payload,
                    0x20000U + static_cast<uint32_t>(header_size + bodies.size()));
            codec_test::wire::word(payload, archive.destination);
            bodies += archive.compressed;
            config.ranges.push_back({archive.destination, archive.expected.size()});
        }
        payload += bodies;
        asm_test::settings settings;
        settings.readable_padding  = 0;
        settings.instruction_limit = 4000000;
        settings.cycle_limit       = 100000000;
        settings.modules           = &config;
        auto       result          = asm_test::execute(image, payload, settings);
        auto const expected        = expected_vram(archives);
        auto const matches         = result.vram == expected;
        auto const pass
                = expect_complete
                          ? result.completed && matches
                                    && (config.interrupt_outside_decoder
                                                ? result.bookmarks.empty()
                                                : result.bookmarks
                                                          == result.interrupted_pcs)
                          : !result.completed && !result.diagnostic.empty();
        uint64_t digest = 14695981039346656037ULL;
        for (unsigned char c : payload) {
            digest = (digest ^ c) * 1099511628211ULL;
        }
        static std::ofstream report([] {
            auto const* path = std::getenv("MDCOMP_MODULE_REPORT");
            return path ? path : "module-checks.tsv";
        }());
        report << name << '\t' << digest << '\t' << archives.size() << '\t' << flags
               << '\t' << config.interrupt_after << '\t' << config.interrupt_period
               << '\t' << config.interrupt_outside_decoder << '\t'
               << config.trace_decoder_pcs << '\t' << expect_complete << '\t'
               << result.completed << '\t' << matches << '\t' << result.dma.size() << '\t'
               << result.interrupted_pcs.size() << '\t' << result.bookmarks.size() << '\t'
               << result.instructions << '\t' << result.cycles << '\t' << pass << '\n';
        report.flush();
        INFO(name << "\n" << result.diagnostic);
        if (!pass) {
            auto path = std::filesystem::path("failures/asm") / name;
            std::filesystem::create_directories(path);
            std::ofstream(path / "input.bin", std::ios::binary)
                    .write(payload.data(), static_cast<std::streamsize>(payload.size()));
            std::ofstream(path / "machine.txt") << result.diagnostic;
            std::ofstream(path / "actual.bin", std::ios::binary)
                    .write(reinterpret_cast<char const*>(result.vram.data()),
                           static_cast<std::streamsize>(result.vram.size()));
            std::ofstream(path / "expected.bin", std::ios::binary)
                    .write(reinterpret_cast<char const*>(expected.data()),
                           static_cast<std::streamsize>(expected.size()));
        }
        return result;
    }

    void verify(
            asm_test::result const& result, std::vector<archive> const& archives,
            bool bookmarked = true) {
        INFO(result.diagnostic);
        REQUIRE(result.completed);
        auto const expected           = expected_vram(archives);
        size_t     expected_transfers = 0;
        for (auto const& archive : archives) {
            expected_transfers += (archive.expected.size() + 4095) / 4096;
        }
        REQUIRE(result.vram.size() == expected.size());
        auto const mismatch
                = std::mismatch(result.vram.begin(), result.vram.end(), expected.begin());
        INFO("VRAM first mismatch offset "
             << std::distance(result.vram.begin(), mismatch.first));
        CHECK((result.vram == expected));
        CHECK(result.dma.size() == expected_transfers);
        size_t transfer = 0;
        for (auto const& archive : archives) {
            for (size_t offset = 0; offset < archive.expected.size(); offset += 4096) {
                REQUIRE(transfer < result.dma.size());
                auto const& dma = result.dma[transfer++];
                CHECK(dma.source == 0xff9000);
                CHECK(dma.destination == uint16_t(archive.destination + offset));
                CHECK(dma.words
                      == std::min(size_t{4096}, archive.expected.size() - offset) / 2);
            }
        }
        if (bookmarked) {
            CHECK(result.bookmarks == result.interrupted_pcs);
        } else {
            CHECK(result.bookmarks.empty());
        }
    }

    archive known(std::string_view codec, std::string_view suffix = "/literal-16") {
        if (suffix == "long-even") {
            bool const                    plus = codec == "kosplus";
            codec_test::wire::descriptors wire(plus ? 8 : 16, !plus, true, !plus);
            wire.bit(1);
            wire.put('A');
            wire.bit(1);
            wire.put('A');
            wire.bits(1, 2);
            wire.put(plus ? 0xf8 : 0xff);
            wire.put(plus ? 0xff : 0xf8);
            wire.put(0xff);    // 264 bytes for Kosinski Plus, 256 for Kosinski.
            wire.bits(1, 2);
            wire.put(0);
            wire.put(0);
            wire.put(0);
            std::string header;
            auto const  length = plus ? 266U : 258U;
            codec_test::wire::word(header, length);
            return {header + wire.data, std::string(length, 'A')};
        }
        for (auto const& f : codec_test::known_answers()) {
            if (f.codec == codec && f.id.ends_with(suffix)) {
                std::string data;
                codec_test::wire::word(data, static_cast<unsigned>(f.expected.size()));
                return {data + f.compressed, f.expected};
            }
        }
        throw std::runtime_error("Missing module bootstrap fixture");
    }

    codec_test::api const& api(std::string_view codec) {
        for (auto const& a : codec_test::codecs()) {
            if (a.name == codec) {
                return a;
            }
        }
        throw std::runtime_error("Unknown module codec");
    }
}    // namespace

TEST_CASE(
        "Moduled queues bootstrap with independent literal streams",
        "[asm][module-bootstrap]") {
    for (auto codec : {"comper", "comperx", "kosinski", "kosplus"}) {
        for (unsigned words : {0U, 1U}) {
            auto match = std::string_view(codec) == "comper"    ? "/overlap-256"
                         : std::string_view(codec) == "comperx" ? "/overlap-254"
                                                                : "long-even";
            for (std::string_view suffix :
                 {std::string_view("/literal-16"), std::string_view(match)}) {
                std::vector<archive> const archives{known(codec, suffix)};
                auto const                 baseline = run(codec, words, archives);
                verify(baseline, archives);
                CHECK(baseline.interrupted_pcs.empty());
                // Interrupt every distinct executed decoder instruction in this small
                // independent stream, rather than guessing safe hand-picked PCs.
                asm_test::module_configuration traced;
                traced.trace_decoder_pcs = true;
                auto const trace         = run(codec, words, archives, traced);
                verify(trace, archives);
                std::vector<uint32_t> covered;
                for (size_t i = 0; i < trace.decoder_pcs.size(); ++i) {
                    auto pc = trace.decoder_pcs[i];
                    if (std::find(covered.begin(), covered.end(), pc) != covered.end()) {
                        continue;
                    }
                    covered.push_back(pc);
                    asm_test::module_configuration interrupt;
                    interrupt.interrupt_after = i + 1;
                    interrupt.max_interrupts  = 1;
                    auto const resumed        = run(codec, words, archives, interrupt);
                    INFO(codec << " interruption at " << std::hex << pc);
                    verify(resumed, archives);
                    REQUIRE(resumed.interrupted_pcs.size() == 1);
                    CHECK(resumed.interrupted_pcs.front() == pc);
                }
            }
        }
    }
}

TEST_CASE(
        "Moduled queues decode C++ archives with partial final modules and DMA",
        "[asm][module-encode]") {
    for (auto codec : {"comper", "comperx", "kosinski", "kosplus"}) {
        auto const& a = api(codec);
        for (size_t size :
             {size_t{2}, size_t{4094}, size_t{4096}, size_t{4098}, size_t{8192},
              size_t{8194}, size_t{32768}}) {
            for (unsigned pattern : {0U, 1U, 4U}) {
                auto const input   = codec_test::corpus(size, pattern);
                auto const encoded = codec_test::run(a.moduled_encode, input);
                REQUIRE(encoded.success);
                std::vector<archive> const archives{
                        {encoded.bytes, input}
                };
                for (unsigned words : {0U, 1U}) {
                    CAPTURE(codec, size, pattern, words);
                    verify(run(codec, words, archives), archives);
                    if (size == 4098 && pattern == 1) {
                        asm_test::module_configuration interrupt;
                        interrupt.interrupt_after  = 3;
                        interrupt.interrupt_period = 19;
                        auto const resumed = run(codec, words, archives, interrupt);
                        verify(resumed, archives);
                        CHECK(resumed.interrupted_pcs.size() > 1);
                    }
                }
            }
        }
    }
}

TEST_CASE(
        "Moduled archive FIFOs preserve destinations and respect capacity",
        "[asm][module-bootstrap]") {
    for (auto codec : {"comper", "comperx", "kosinski", "kosplus"}) {
        for (unsigned words : {0U, 1U}) {
            auto const           comp = std::string_view(codec).starts_with("comper");
            std::vector<archive> archives{
                    known(codec, comp ? "/literal-1" : "/literal-16"),
                    known(codec, comp ? "/literal-15" : "/match-0-5"),
                    known(codec, comp ? "/literal-16" : "/match-1-3"),
                    known(codec, comp ? "/literal-17" : "/literal-32")};
            for (size_t i = 0; i < archives.size(); ++i) {
                archives[i].destination = static_cast<uint16_t>(0x1200 + i * 0x100);
            }
            verify(run(codec, words, archives), archives);
            archives.push_back(known(codec));
            auto const overflow = run(codec, words, archives, {}, 0, false);
            CHECK_FALSE(overflow.completed);
            CHECK(overflow.dma.empty());
        }
    }
}

TEST_CASE(
        "Module pipelines handle empty archives backpressure and VRAM wrap",
        "[asm][module-bootstrap]") {
    for (auto codec : {"comper", "comperx", "kosinski", "kosplus"}) {
        for (unsigned words : {0U, 1U}) {
            asm_test::module_configuration bounded;
            bounded.frame_limit = 20;
            verify(run(codec, words, {}, bounded), {});
            archive              empty{std::string(2, '\0'), ""};
            std::vector<archive> archives{empty, known(codec), empty, known(codec)};
            archives.back().destination = 0x2300;
            verify(run(codec, words, archives, bounded), archives);
            std::vector<archive>           one{known(codec)};
            asm_test::module_configuration outside;
            outside.interrupt_after           = 1;
            outside.max_interrupts            = 1;
            outside.interrupt_outside_decoder = true;
            auto const no_bookmark            = run(codec, words, one, outside);
            verify(no_bookmark, one, false);
            CHECK(no_bookmark.interrupted_pcs.size() == 1);
            verify(run(codec, words, one, {}, 1), one);    // Raw FIFO initially full.
            one.front().destination = 0xfff0;
            verify(run(codec, words, one), one);
        }
    }
}

TEST_CASE(
        "Kosinski module headers preserve the legacy A000 size alias",
        "[asm][module-encode]") {
    auto const raw     = codec_test::corpus(0x8000, 2);
    auto       encoded = codec_test::run(api("kosinski").moduled_encode, raw);
    REQUIRE(encoded.success);
    encoded.bytes[0] = static_cast<char>(0xa0);
    std::vector<archive> const archives{
            {encoded.bytes, raw}
    };
    for (unsigned words : {0U, 1U}) {
        verify(run("kosinski", words, archives), archives);
    }
}

TEST_CASE(
        "The DMA shim detects reuse before drain and malformed sizes",
        "[asm][module-encode]") {
    for (auto codec : {"comper", "comperx", "kosinski", "kosplus"}) {
        auto const raw     = codec_test::corpus(8192, 2);
        auto const encoded = codec_test::run(api(codec).moduled_encode, raw);
        REQUIRE(encoded.success);
        std::vector<archive> archives{
                {encoded.bytes, raw}
        };
        asm_test::module_configuration deferred;
        deferred.defer_dma_flushes = 3;
        auto const late            = run(codec, 0, archives, deferred, 0, false);
        CHECK_FALSE(late.completed);
        CHECK(late.diagnostic.find("overwritten before drain") != std::string::npos);
        archives[0].compressed[0] = 0;
        archives[0].compressed[1] = 2;    // Claims 2 bytes but the module produces 4096.
        auto const malformed      = run(codec, 0, archives, {}, 0, false);
        CHECK_FALSE(malformed.completed);
        CHECK(malformed.diagnostic.find("DMA size differs") != std::string::npos);
    }
}
