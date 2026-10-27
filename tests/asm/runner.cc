#include "decoder_registry.hh"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
    uint64_t number(std::string_view text) {
        uint64_t value = 0;
        int      base  = 10;
        if (text.starts_with("0x") || text.starts_with("0X")) {
            text.remove_prefix(2);
            base = 16;
        }
        auto const parsed
                = std::from_chars(text.data(), text.data() + text.size(), value, base);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
            throw std::runtime_error("Invalid unsigned integer: " + std::string(text));
        }
        return value;
    }

    size_t count(std::string_view text) {
        auto value = number(text);
        if (!std::in_range<size_t>(value)) {
            throw std::runtime_error("Count exceeds host size_t");
        }
        return static_cast<size_t>(value);
    }

    bool aliases(std::filesystem::path const& a, std::filesystem::path const& b) {
        return std::filesystem::weakly_canonical(a)
                       == std::filesystem::weakly_canonical(b)
               || (std::filesystem::exists(a) && std::filesystem::exists(b)
                   && std::filesystem::equivalent(a, b));
    }

    void help() {
        std::cout
                << "Usage: mdcomp-asm-run DECODER INPUT OUTPUT [options]\n"
                   "       mdcomp-asm-run --list\n"
                   "  --output-size N        Require exactly N decoded bytes\n"
                   "  --output-limit N       Cap output (default: machine capacity)\n"
                   "  --image FILE           Run a custom ASL wrapper image\n"
                   "  --image-dir DIR        Find built decoder images in DIR\n"
                   "  --art-base N           Enigma starting tile (default 0)\n"
                   "  --compressed-size N    SaxDec2 body length (default input size)\n"
                   "  --instruction-limit N  Instruction budget (default 2000000)\n"
                   "  --cycle-limit N        Clock/T-state budget (default 50000000)\n"
                   "  --json                 Write machine-readable timing to stdout\n"
                   "Numbers accept decimal or 0x hexadecimal. Reset/setup is excluded;\n"
                   "the final RET/RTS is included. 68000 m(r/w) bus counts are Musashi\n"
                   "logical 16-bit transfers, not a cycle-exact physical bus trace.\n";
    }
}    // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--list") {
            for (auto const& decoder : asm_test::decoder_images()) {
                std::cout << decoder.name << '\t'
                          << (decoder.architecture == asm_test::cpu::m68000 ? "68000"
                                                                            : "z80")
                          << '\t' << decoder.configuration << '\n';
            }
            return 0;
        }
        if (argc == 2
            && (std::string_view(argv[1]) == "--help"
                || std::string_view(argv[1]) == "-h")) {
            help();
            return 0;
        }
        if (argc < 4) {
            help();
            return 2;
        }
        auto const decoders = asm_test::decoder_images();
        auto const found
                = std::find_if(decoders.begin(), decoders.end(), [&](auto const& d) {
                      return d.name == argv[1];
                  });
        if (found == decoders.end()) {
            throw std::runtime_error("Unknown decoder; use --list");
        }
        auto const&        decoder = *found;
        auto const         is68k   = decoder.architecture == asm_test::cpu::m68000;
        asm_test::settings config;
        auto const capacity        = is68k                               ? size_t{0x80000}
                                     : decoder.name == "sega-saxman-z80" ? size_t{0x800}
                                                                         : size_t{0x6000};
        config.output_size         = capacity;
        config.exact_output_size   = false;
        config.readable_padding    = decoder.readable_padding;
        config.self_modifying      = decoder.self_modifying;
        config.stack_scratch_bytes = decoder.stack_scratch_bytes;
        auto                  directory = asm_test::image_directory();
        std::filesystem::path custom_image;
        bool                  json = false, have_output_bound = false;
        for (int i = 4; i < argc; ++i) {
            std::string_view const option(argv[i]);
            if (option == "--json") {
                json = true;
                continue;
            }
            if (i + 1 == argc) {
                throw std::runtime_error("Missing value for " + std::string(option));
            }
            std::string_view const value(argv[++i]);
            if (option == "--image") {
                custom_image = value;
            } else if (option == "--image-dir") {
                directory = value;
            } else if (option == "--output-size" || option == "--output-limit") {
                if (have_output_bound) {
                    throw std::runtime_error("Specify only one output size/limit");
                }
                have_output_bound        = true;
                config.output_size       = count(value);
                config.exact_output_size = option == "--output-size";
            } else if (option == "--art-base") {
                auto const tile = number(value);
                if (tile > 0xffff || decoder.codec != "enigma") {
                    throw std::runtime_error(
                            "Art base requires Enigma and a 16-bit value");
                }
                config.art_base = static_cast<uint16_t>(tile);
            } else if (option == "--compressed-size") {
                auto const bytes = number(value);
                if (!decoder.headerless || bytes > 0xffff) {
                    throw std::runtime_error(
                            "Compressed size requires SaxDec2 and a 16-bit value");
                }
                config.body_size = static_cast<uint16_t>(bytes);
            } else if (option == "--instruction-limit") {
                config.instruction_limit = count(value);
            } else if (option == "--cycle-limit") {
                config.cycle_limit = number(value);
            } else {
                throw std::runtime_error("Unknown option: " + std::string(option));
            }
        }
        if (config.output_size > capacity || !config.instruction_limit
            || !config.cycle_limit) {
            throw std::runtime_error(
                    "Output exceeds machine capacity or execution budget is zero");
        }
        auto const image_path = custom_image.empty()
                                        ? directory / (std::string(decoder.name) + ".bin")
                                        : custom_image;
        std::filesystem::path const input_path(argv[2]), output_path(argv[3]);
        if (aliases(input_path, output_path) || aliases(image_path, output_path)) {
            throw std::runtime_error(
                    "Output aliases the compressed input or decoder image");
        }
        auto const input_limit = (is68k ? 0x20000U : 0x4000U) - config.readable_padding;
        if (std::filesystem::file_size(input_path) > input_limit) {
            throw std::runtime_error("Compressed input exceeds machine capacity");
        }
        std::ifstream source(input_path, std::ios::binary);
        if (!source) {
            throw std::runtime_error("Cannot open compressed input");
        }
        std::string const input((std::istreambuf_iterator<char>(source)), {});
        if (source.bad()) {
            throw std::runtime_error("Cannot read compressed input");
        }
        if (decoder.headerless && !config.body_size) {
            if (input.size() > 0xffff) {
                throw std::runtime_error("SaxDec2 body exceeds 65535 bytes");
            }
            config.body_size = static_cast<uint16_t>(input.size());
        }
        auto const image = asm_test::load_image(
                image_path, decoder.architecture, decoder.self_modifying);
        auto const result = asm_test::execute(image, input, config);
        if (!result.completed) {
            std::cerr << "Decompression failed after "
                      << asm_test::timing(decoder.architecture, result) << ": "
                      << result.diagnostic << '\n';
            return 1;
        }
        std::ofstream destination(output_path, std::ios::binary | std::ios::trunc);
        destination.write(
                result.bytes.data(), static_cast<std::streamsize>(result.bytes.size()));
        destination.flush();
        if (!destination) {
            throw std::runtime_error("Cannot write decoded output");
        }
        if (json) {
            std::cout << "{\"decoder\":\"" << decoder.name << "\",\"cpu\":\""
                      << (is68k ? "68000" : "z80")
                      << "\",\"output_bytes\":" << result.bytes.size()
                      << ",\"configuration\":\"" << decoder.configuration << '"'
                      << ",\"instructions\":" << result.instructions
                      << ",\"cycles\":" << result.cycles << ",\"timing\":\""
                      << asm_test::timing(decoder.architecture, result)
                      << "\",\"timing_model\":\""
                      << (is68k ? "musashi-logical-16-bit-transfers" : "z80-core-tstates")
                      << '"';
            if (is68k) {
                std::cout << ",\"bus_reads\":" << result.bus_reads
                          << ",\"bus_writes\":" << result.bus_writes;
            }
            std::cout << ",\"input_high_water\":" << result.input_high_water
                      << ",\"dependencies\":\"" << asm_test::dependency_versions()
                      << "\"}\n";
        } else {
            std::cout << "Assembly dependencies: " << asm_test::dependency_versions()
                      << '\n';
            std::cout << decoder.name << ": " << result.bytes.size() << " bytes, "
                      << asm_test::timing(decoder.architecture, result) << ", "
                      << result.instructions << " instructions\n";
            if (is68k) {
                std::cout << "Bus counts: Musashi logical 16-bit transfers; some "
                             "hardware prefetch/dummy reads are not modeled.\n";
            }
        }
        return 0;
    } catch (std::exception const& error) {
        std::cerr << "mdcomp-asm-run: " << error.what() << '\n';
        return 1;
    }
}
