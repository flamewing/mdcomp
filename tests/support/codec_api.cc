#include "codec_api.hh"

#include "mdcomp/comper.hh"
#include "mdcomp/comperx.hh"
#include "mdcomp/enigma.hh"
#include "mdcomp/kosinski.hh"
#include "mdcomp/kosplus.hh"
#include "mdcomp/lzkn1.hh"
#include "mdcomp/nemesis.hh"
#include "mdcomp/rocket.hh"
#include "mdcomp/saxman.hh"
#include "mdcomp/snkrle.hh"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace codec_test {
    namespace {
        template <typename Codec>
        bool encode(std::istream& input, std::iostream& output, options const& settings) {
            if constexpr (std::same_as<Codec, saxman>) {
                return Codec::encode(input, output, settings.size_header);
            } else {
                return Codec::encode(input, output);
            }
        }

        template <typename Codec>
        bool decode(std::istream& input, std::iostream& output, options const& settings) {
            if constexpr (std::same_as<Codec, saxman>) {
                return Codec::decode(
                        input, output,
                        settings.size_header ? 0 : settings.compressed_size);
            } else {
                return Codec::decode(input, output);
            }
        }

        template <typename Codec>
        bool encode_modules(
                std::istream& input, std::iostream& output, options const& settings) {
            return Codec::moduled_encode(
                    input, output,
                    settings.module_padding ? settings.module_padding
                                            : Codec::default_module_padding);
        }

        template <typename Codec>
        bool decode_modules(
                std::istream& input, std::iostream& output, options const& settings) {
            return Codec::moduled_decode(
                    input, output,
                    settings.module_padding ? settings.module_padding
                                            : Codec::default_module_padding);
        }

        template <typename Codec>
        api basic(std::string_view name, size_t grain, size_t alignment) {
            return {name, grain, alignment, encode<Codec>, decode<Codec>};
        }

        template <typename Codec>
        api moduled(std::string_view name, size_t grain, size_t alignment) {
            auto value           = basic<Codec>(name, grain, alignment);
            value.moduled_encode = encode_modules<Codec>;
            value.moduled_decode = decode_modules<Codec>;
            value.module_padding = Codec::default_module_padding;
            return value;
        }
    }    // namespace

    std::array<api, 10> const& codecs() {
        static std::array const values{
                moduled<comper>("comper", 2, 2),   moduled<comperx>("comperx", 2, 2),
                basic<enigma>("enigma", 2, 2),     moduled<kosinski>("kosinski", 1, 1),
                moduled<kosplus>("kosplus", 1, 1), moduled<lzkn1>("lzkn1", 1, 1),
                basic<nemesis>("nemesis", 1, 32),  moduled<rocket>("rocket", 1, 1),
                moduled<saxman>("saxman", 1, 1),   moduled<snkrle>("snkrle", 2, 2)};
        return values;
    }

#define MDCOMP_CODEC_API(Codec, Index) \
    template <>                        \
    api const& for_codec<Codec>() {    \
        return codecs()[Index];        \
    }
    MDCOMP_CODEC_API(comper, 0)
    MDCOMP_CODEC_API(comperx, 1)
    MDCOMP_CODEC_API(enigma, 2)
    MDCOMP_CODEC_API(kosinski, 3)
    MDCOMP_CODEC_API(kosplus, 4)
    MDCOMP_CODEC_API(lzkn1, 5)
    MDCOMP_CODEC_API(nemesis, 6)
    MDCOMP_CODEC_API(rocket, 7)
    MDCOMP_CODEC_API(saxman, 8)
    MDCOMP_CODEC_API(snkrle, 9)
#undef MDCOMP_CODEC_API

    result run(
            operation function, std::string const& input, options const& settings,
            size_t offset, std::string_view output_prefix) {
        if (!function || offset > input.size()) {
            throw std::invalid_argument("Invalid codec test operation");
        }
        std::istringstream source(input, std::ios::in | std::ios::binary);
        source.seekg(static_cast<std::streamoff>(offset));
        std::stringstream output(std::ios::in | std::ios::out | std::ios::binary);
        if (!output_prefix.empty()) {
            output.write(
                    output_prefix.data(),
                    static_cast<std::streamsize>(output_prefix.size()));
        }
        std::ostringstream diagnostics;

        struct redirect {
            std::streambuf* previous;

            explicit redirect(std::ostream& stream)
                    : previous(std::cerr.rdbuf(stream.rdbuf())) {}

            ~redirect() {
                std::cerr.rdbuf(previous);
            }
        } capture(diagnostics);

        result value;
        try {
            value.success = function(source, output, settings);
        } catch (std::exception const& error) {
            value.diagnostic = error.what();
        } catch (...) {
            value.diagnostic = "Non-standard codec exception";
        }
        value.input_state  = source.rdstate();
        value.output_state = output.rdstate();
        if (!source.fail()) {
            auto const position = source.tellg();
            if (position != std::streampos{-1}
                && position >= std::streampos{static_cast<std::streamoff>(offset)}) {
                value.consumed = static_cast<size_t>(position) - offset;
            }
        }
        value.bytes = output.str();
        value.diagnostic += diagnostics.str();
        value.success = value.success && !output.fail();
        return value;
    }

    std::string padded(api const& codec, std::string input) {
        auto const remainder = input.size() % codec.output_alignment;
        if (remainder) {
            input.append(codec.output_alignment - remainder, '\0');
        }
        return input;
    }

    std::string corpus(size_t size, unsigned pattern, uint32_t seed) {
        std::string bytes(size, '\0');
        for (size_t i = 0; i < size; ++i) {
            switch (pattern) {
            case 0:
                break;
            case 1:
                bytes[i] = static_cast<char>(0xa5);
                break;
            case 2:
                bytes[i] = static_cast<char>(i & 0xff);
                break;
            case 3:
                bytes[i] = static_cast<char>((i / 17) & 1 ? 0xff : 0);
                break;
            case 5:
                bytes[i] = ' ';
                break;
            case 6:
                bytes[i] = i < 4096 ? static_cast<char>(0xa5) : ' ';
                break;
            default:
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                bytes[i] = static_cast<char>(seed & 0xff);
                break;
            }
        }
        return bytes;
    }

    void save_failure(
            std::string_view codec, std::string_view case_name, std::string const& input,
            result const& encoded, result const& decoded, uint32_t seed) {
        auto const directory = std::filesystem::path("failures") / codec / case_name;
        std::filesystem::create_directories(directory);
        auto write = [&](char const* name, std::string const& bytes) {
            std::ofstream file(directory / name, std::ios::binary);
            file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        };
        write("input.bin", input);
        write("encoded.bin", encoded.bytes);
        write("decoded.bin", decoded.bytes);
        std::ofstream metadata(directory / "case.txt");
        metadata << "seed=" << seed << "\nencode=" << encoded.success
                 << "\ndecode=" << decoded.success
                 << "\nencode diagnostic=" << encoded.diagnostic
                 << "\ndecode diagnostic=" << decoded.diagnostic << '\n';
    }
}    // namespace codec_test
