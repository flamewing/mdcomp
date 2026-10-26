#ifndef MDCOMP_TEST_CODEC_API_HH
#define MDCOMP_TEST_CODEC_API_HH

#include <array>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

class comper;
class comperx;
class enigma;
class kosinski;
class kosplus;
class lzkn1;
class nemesis;
class rocket;
class saxman;
class snkrle;

namespace codec_test {
    struct options {
        bool   size_header     = true;
        size_t compressed_size = 0;
        size_t module_padding  = 0;
    };

    using operation = bool (*)(std::istream&, std::iostream&, options const&);

    struct api {
        std::string_view name;
        size_t           input_granularity;
        size_t           output_alignment;
        operation        encode;
        operation        decode;
        operation        moduled_encode = nullptr;
        operation        moduled_decode = nullptr;
        size_t           module_size    = 4096;
        size_t           module_padding = 1;
    };

    struct result {
        bool                  success = false;
        std::string           bytes;
        std::string           diagnostic;
        std::optional<size_t> consumed;
        std::ios::iostate     input_state{};
        std::ios::iostate     output_state{};
    };

    std::array<api, 10> const& codecs();
    template <typename Codec>
    api const& for_codec();
#define MDCOMP_CODEC_API(Codec) \
    template <>                 \
    api const& for_codec<Codec>();
    MDCOMP_CODEC_API(comper)
    MDCOMP_CODEC_API(comperx)
    MDCOMP_CODEC_API(enigma)
    MDCOMP_CODEC_API(kosinski)
    MDCOMP_CODEC_API(kosplus)
    MDCOMP_CODEC_API(lzkn1)
    MDCOMP_CODEC_API(nemesis)
    MDCOMP_CODEC_API(rocket)
    MDCOMP_CODEC_API(saxman)
    MDCOMP_CODEC_API(snkrle)
#undef MDCOMP_CODEC_API

    result run(
            operation function, std::string const& input, options const& settings = {},
            size_t offset = 0, std::string_view output_prefix = {});
    std::string padded(api const& codec, std::string input);
    std::string corpus(size_t size, unsigned pattern, uint32_t seed = 0x12345678);
    void        save_failure(
            std::string_view codec, std::string_view case_name, std::string const& input,
            result const& encoded, result const& decoded, uint32_t seed);
}    // namespace codec_test
#endif
