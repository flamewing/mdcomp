#include "decoder_registry.hh"
#include "machine.hh"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("ASL images execute and return through both CPU sentinels", "[asm][machine]") {
    for (auto architecture : {asm_test::cpu::m68000, asm_test::cpu::z80}) {
        auto const image = asm_test::load_image(
                std::filesystem::path(MDCOMP_ASM_IMAGES)
                        / (architecture == asm_test::cpu::m68000 ? "copy68k.bin"
                                                                 : "copyz80.bin"),
                architecture);
        asm_test::settings config;
        config.output_size      = 4;
        config.readable_padding = 0;
        auto const result       = asm_test::execute(image, "test", config);
        CAPTURE(result.diagnostic);
        REQUIRE(result.completed);
        CHECK(result.bytes == "test");
        CHECK(result.input_high_water == 4);
        if (architecture == asm_test::cpu::m68000) {
            // MOVEQ 4; four MOVE.B copies 48; DBRA 30+14; RTS 16.
            CHECK(result.cycles == 112);
            CHECK(result.bus_reads == 19);
            CHECK(result.bus_writes == 4);
            CHECK(asm_test::timing(architecture, result) == "112(19/4)");
        } else {
            // LD BC 10; LDIR 3*21+16; RET 10.
            CHECK(result.cycles == 99);
            CHECK(asm_test::timing(architecture, result) == "99 T-states");
        }
        config.output_size       = 64;
        config.exact_output_size = false;
        auto const capped        = asm_test::execute(image, "test", config);
        REQUIRE(capped.completed);
        CHECK(capped.bytes == "test");
        CHECK(capped.cycles == result.cycles);
        CHECK(capped.bus_reads == result.bus_reads);
        CHECK(capped.bus_writes == result.bus_writes);
        config.exact_output_size = true;
        config.output_size       = 3;
        CHECK_FALSE(asm_test::execute(image, "test", config).completed);
        config.output_size = 4;
        CHECK_FALSE(asm_test::execute(image, "tes", config).completed);
        config.instruction_limit = 1;
        auto const bounded       = asm_test::execute(image, "test", config);
        CHECK_FALSE(bounded.completed);
        CHECK(bounded.diagnostic.find("budget") != std::string::npos);
    }
}

TEST_CASE("68000 longword transfers count two 16-bit bus accesses", "[asm][machine]") {
    auto const image = asm_test::load_image(
            std::filesystem::path(MDCOMP_ASM_IMAGES) / "long68k.bin",
            asm_test::cpu::m68000);
    asm_test::settings config;
    config.output_size      = 4;
    config.readable_padding = 0;
    auto const result       = asm_test::execute(image, "test", config);
    INFO(result.diagnostic);
    REQUIRE(result.completed);
    CHECK(result.bytes == "test");
    CHECK(result.cycles == 36);
    // Two opcodes, two input word transfers, two return-address word reads.
    CHECK(result.bus_reads == 6);
    CHECK(result.bus_writes == 2);
}

TEST_CASE("68000 word accesses enforce original CPU alignment", "[asm][machine]") {
    auto const image = asm_test::load_image(
            std::filesystem::path(MDCOMP_ASM_IMAGES) / "word68k.bin",
            asm_test::cpu::m68000);
    asm_test::settings config;
    config.output_size      = 2;
    config.readable_padding = 0;
    auto const result       = asm_test::execute(image, "xy", config);
    CHECK_FALSE(result.completed);
    CHECK_FALSE(result.diagnostic.empty());
    auto const copy = asm_test::load_image(
            std::filesystem::path(MDCOMP_ASM_IMAGES) / "copy68k.bin",
            asm_test::cpu::m68000);
    config.output_size = 4;
    CHECK(asm_test::execute(copy, "test", config).completed);
}

TEST_CASE("Saxman byte headers need only their declared scratch word", "[asm][machine]") {
    for (auto const& d : asm_test::decoder_images()) {
        if (!d.stack_scratch_bytes) {
            continue;
        }
        auto const image = asm_test::load_image(
                asm_test::image_directory() / (std::string(d.name) + ".bin"),
                d.architecture);
        std::string const  input("\x02\0\x01\x41", 4);    // Sized body: one literal A.
        asm_test::settings config;
        config.output_size      = 1;
        config.readable_padding = 0;
        auto const guarded      = asm_test::execute(image, input, config);
        CHECK_FALSE(guarded.completed);
        CHECK(guarded.diagnostic.find("unwritten") != std::string::npos);
        config.stack_scratch_bytes = d.stack_scratch_bytes;
        for (uint8_t poison : {uint8_t{0xa5}, uint8_t{0x5a}}) {
            config.stack_scratch_fill = poison;
            auto const decoded        = asm_test::execute(image, input, config);
            INFO(decoded.diagnostic);
            CHECK(decoded.completed);
            CHECK(decoded.bytes == "A");
        }
    }
}
