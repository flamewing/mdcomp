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
        bool                  moduled = false, dma_source_words = false;
        uint32_t              interrupt_entry = 0, decoder_begin = 0, decoder_end = 0;
    };

    image load_image(
            std::filesystem::path const& path, cpu architecture,
            bool self_modifying = false);

    struct vram_range {
        uint16_t address;
        size_t   bytes;
    };

    struct module_configuration {
        std::vector<vram_range> ranges;
        size_t interrupt_after = 0, interrupt_period = 0, max_interrupts = 10000;
        bool   trace_decoder_pcs         = false;
        bool   interrupt_outside_decoder = false;
        size_t defer_dma_flushes         = 0;
        size_t frame_limit               = 10000;
    };

    struct dma_transfer {
        uint32_t source;
        uint16_t destination, words;
    };

    struct settings {
        size_t                      output_size         = 0;
        size_t                      readable_padding    = 4;
        size_t                      instruction_limit   = 2000000;
        uint64_t                    cycle_limit         = 50000000;
        uint16_t                    art_base            = 0;
        bool                        self_modifying      = false;
        bool                        exact_output_size   = true;
        size_t                      stack_scratch_bytes = 0;
        uint8_t                     stack_scratch_fill  = 0xa5;
        module_configuration const* modules             = nullptr;
    };

    struct result {
        bool                 completed = false;
        std::string          bytes, diagnostic;
        size_t               instructions = 0, bytes_written = 0, input_high_water = 0;
        uint64_t             cycles = 0, bus_reads = 0, bus_writes = 0;
        size_t               output_high_water = 0;
        std::vector<uint8_t> vram, control_ram;
        std::vector<dma_transfer> dma;
        std::vector<uint32_t>     decoder_pcs, interrupted_pcs, bookmarks;
        size_t                    decoder_visits = 0;
    };

    // 68000 counts are logical 16-bit transfers reported by Musashi, including
    // opcode/extension reads, not a cycle-exact physical bus trace.
    std::string timing(cpu architecture, result const& execution);

    result execute(
            image const& program, std::string const& input, settings const& config);
}    // namespace asm_test
#endif
