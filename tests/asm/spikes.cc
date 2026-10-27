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
        config.output_size = 3;
        CHECK_FALSE(asm_test::execute(image, "test", config).completed);
        config.output_size = 4;
        CHECK_FALSE(asm_test::execute(image, "tes", config).completed);
        config.instruction_limit = 1;
        auto const bounded       = asm_test::execute(image, "test", config);
        CHECK_FALSE(bounded.completed);
        CHECK(bounded.diagnostic.find("budget") != std::string::npos);
    }
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
