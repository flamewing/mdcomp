#ifndef MDCOMP_TEST_ASM_DECODER_REGISTRY_HH
#define MDCOMP_TEST_ASM_DECODER_REGISTRY_HH
#include "machine.hh"

#include <filesystem>
#include <span>
#include <string_view>

namespace asm_test {
    struct decoder {
        std::string_view name, codec;
        cpu              architecture;
        bool             original, self_modifying;
        size_t           readable_padding;
        std::string_view configuration;
        size_t           stack_scratch_bytes;
    };

    constexpr inline decoder compiled_decoders[]{
#include "decoder_images.inc"
    };

    constexpr std::span<decoder const> decoder_images() {
        return compiled_decoders;
    }

    inline std::filesystem::path image_directory() {
        return MDCOMP_ASM_IMAGES;
    }
}    // namespace asm_test
#endif
