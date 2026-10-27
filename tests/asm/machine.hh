#ifndef MDCOMP_TEST_ASM_MACHINE_HH
#define MDCOMP_TEST_ASM_MACHINE_HH
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace asm_test {
    enum class cpu {
        m68000,
        z80
    };

    struct image {
        cpu                   architecture;
        std::vector<uint8_t>  bytes{};
        uint32_t              origin = 0x1000, entry = 0, payload = 0, completion = 0;
        std::vector<uint32_t> patch_words{};
    };

    image load_image(
            std::filesystem::path const& path, cpu architecture,
            bool self_modifying = false);

    struct settings {
        size_t   output_size       = 0;
        size_t   readable_padding  = 4;
        size_t   instruction_limit = 2000000;
        uint64_t cycle_limit       = 50000000;
        uint16_t art_base          = 0;
        bool     self_modifying    = false;
        bool     exact_output_size = true;
    };

    struct result {
        bool        completed = false;
        std::string bytes, diagnostic;
        size_t      instructions = 0, bytes_written = 0, input_high_water = 0;
        uint64_t    cycles = 0, bus_reads = 0, bus_writes = 0;
        size_t      output_high_water = 0;
    };

    // 68000 counts are logical 16-bit transfers reported by Musashi, including
    // opcode/extension reads, not a cycle-exact physical bus trace.
    std::string timing(cpu architecture, result const& execution);

    result execute(
            image const& program, std::string const& input, settings const& config);
}    // namespace asm_test
#endif
