/*
 * Copyright (C) Flamewing 2022 <flamewing.sonic@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIB_OPTIONS_LIB_HH
#define LIB_OPTIONS_LIB_HH

#include <getopt.h>

#include <boost/io/ios_state.hpp>
#include <boost/io_fwd.hpp>

#include <array>
#include <charconv>
#include <concepts>    // IWYU pragma: keep
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>

template <auto* long_options>
requires requires(decltype(long_options) opt) {
    { opt->size() } -> std::same_as<size_t>;
    { opt->data() } -> std::same_as<option const*>;
}
consteval inline auto make_short_options() {
    static_assert(long_options->back().name == nullptr);
    constexpr auto const result = [&]() consteval noexcept {
        std::array<char, 3U * (long_options->size() - 1U)> intermediate{};

        size_t length = 0;
        for (auto const& opt : *long_options) {
            if (opt.name == nullptr) {
                break;
            }
            char const val = static_cast<char>(opt.val);
            if (val == '\0') {
                // Allow options without a short form.
                continue;
            }
            intermediate[length++] = val;
            switch (opt.has_arg) {
            case no_argument:
                break;
            case optional_argument:
                intermediate[length++] = ':';
                intermediate[length++] = ':';
                break;
            case required_argument:
                intermediate[length++] = ':';
                break;
            }
        }
        return std::pair{intermediate, length};
    }();
    auto const to_init = [&]<size_t... Is>(std::index_sequence<Is...>) {
        return std::array{result.first[Is]..., '\0'};
    };
    return to_init(std::make_index_sequence<result.second>());
}

template <typename instream, typename outstream, typename... Args>
inline auto gen_argument_tuple(instream& input, outstream& output, Args&&... args) {
    return std::tuple{std::ref(input), std::ref(output), std::forward<Args>(args)...};
}

#define FWD(x) static_cast<decltype(x)&&>(x)
#define RETURNS(expr)                          \
    noexcept(noexcept(expr))->decltype(expr) { \
        return expr;                           \
    }
#define OVERLOADS_OF(name) [&](auto&&... args) RETURNS(name(FWD(args)...))

namespace detail {
    [[noreturn]] inline void print_error(
            std::errc error, std::string const& parameter, char const* value) {
        if (error == std::errc::invalid_argument) {
            std::cerr << "Invalid value '" << value << "' given for '" << parameter
                      << "' parameter!\n";
        } else if (error == std::errc::result_out_of_range) {
            std::cerr << "The value '" << value << "' given for '" << parameter
                      << "' parameter is out of range!\n";
        } else {
            std::cerr << "Unknown error happened when parsing value '" << value
                      << "' given for '" << parameter << "' parameter!\n";
        }
        throw 5;
    }

    template <typename options_t>
    concept has_crunch = requires(options_t opt) {
        { opt.crunch } -> std::same_as<bool&>;
    };
    template <typename options_t>
    concept has_moduled = requires(options_t opt) {
        { opt.moduled } -> std::same_as<bool&>;
    };
    template <typename options_t>
    concept has_print_end = requires(options_t opt) {
        { opt.print_end } -> std::same_as<bool&>;
    };
    template <typename options_t>
    concept has_pointer = requires(options_t opt) {
        { opt.pointer } -> std::same_as<std::streamsize&>;
    };
    template <typename options_t>
    concept has_padding = requires(options_t opt) {
        { opt.padding } -> std::same_as<size_t&>;
    };
    template <typename options_t>
    concept has_size = requires(options_t opt) {
        { opt.size } -> std::same_as<size_t&>;
    };
    template <typename options_t>
    concept has_with_size = requires(options_t opt) {
        { opt.with_size } -> std::same_as<bool&>;
    };
    template <typename options_t>
    concept has_get_decode_args
            = requires(options_t opt, std::istream& instream, std::ostream& outstream) {
                  { opt.get_decode_args(instream, outstream) };
              };
    template <typename options_t>
    concept has_get_moduled_decode_args
            = requires(options_t opt, std::istream& instream, std::ostream& outstream) {
                  { opt.get_moduled_decode_args(instream, outstream) };
              };
    template <typename options_t>
    concept has_get_encode_args
            = requires(options_t opt, std::istream& instream, std::ostream& outstream) {
                  { opt.get_encode_args(instream, outstream) };
              };
    template <typename options_t>
    concept has_get_moduled_encode_args
            = requires(options_t opt, std::istream& instream, std::ostream& outstream) {
                  { opt.get_moduled_encode_args(instream, outstream) };
              };

    template <typename options_t, typename instream, typename outstream>
    [[nodiscard]] auto get_decode_args(
            options_t options, instream& input, outstream& output) {
        if constexpr (has_get_decode_args<options_t>) {
            return options.get_decode_args(input, output);
        } else {
            return gen_argument_tuple(input, output);
        }
    }

    template <typename options_t, typename instream, typename outstream>
    [[nodiscard]] auto get_moduled_decode_args(
            options_t options, instream& input, outstream& output) {
        if constexpr (has_get_moduled_decode_args<options_t>) {
            return options.get_moduled_decode_args(input, output);
        } else {
            return gen_argument_tuple(input, output);
        }
    }

    template <typename options_t, typename instream, typename outstream>
    [[nodiscard]] auto get_encode_args(
            options_t options, instream& input, outstream& output) {
        if constexpr (has_get_encode_args<options_t>) {
            return options.get_encode_args(input, output);
        } else {
            return gen_argument_tuple(input, output);
        }
    }

    template <typename options_t, typename instream, typename outstream>
    [[nodiscard]] auto get_moduled_encode_args(
            options_t options, instream& input, outstream& output) {
        if constexpr (has_get_moduled_encode_args<options_t>) {
            return options.get_moduled_encode_args(input, output);
        } else {
            return gen_argument_tuple(input, output);
        }
    }

    template <typename options_t>
    inline void parse_extract(options_t& options, char const* parameter_in) {
        options.extract = true;
        if (parameter_in != nullptr) {
            std::string_view const parameter(parameter_in);
            auto [ptr, ec] = std::from_chars(
                    std::ranges::cbegin(parameter), std::ranges::cend(parameter),
                    options.pointer);
            if (ec != std::errc{}) {
                print_error(ec, "pointer", parameter_in);
            }
        }
    }

    template <typename options_t>
    inline void parse_crunch(options_t& options) {
        if constexpr (has_crunch<options_t>) {
            options.crunch = true;
        }
    }

    template <typename options_t>
    inline void parse_moduled(options_t& options) {
        if constexpr (has_moduled<options_t>) {
            options.moduled = true;
        }
    }

    template <typename options_t>
    inline void parse_padding(options_t& options, char const* parameter_in) {
        if constexpr (has_padding<options_t>) {
            if (parameter_in != nullptr) {
                std::string_view const parameter(parameter_in);
                auto [ptr, ec] = std::from_chars(
                        std::ranges::cbegin(parameter), std::ranges::cend(parameter),
                        options.padding);
                if (ec != std::errc{}) {
                    print_error(ec, "padding", optarg);
                }
            }
            if ((options.padding == 0U) || !std::has_single_bit(options.padding)) {
                options.padding = options_t::format_t::MODULE_PADDING;
            }
        }
    }

    template <typename options_t>
    inline void parse_print_end(options_t& options) {
        if constexpr (has_print_end<options_t>) {
            options.print_end = true;
        }
    }

    template <typename options_t>
    inline void parse_size(options_t& options, char const* parameter) {
        if constexpr (has_size<options_t>) {
            if (parameter != nullptr) {
                options.size = strtoul(parameter, nullptr, 0);
            }
            if (options.size == 0) {
                std::cerr << "Error: specified size must be a positive number.\n\n";
                throw 4;
            }
        }
    }

    template <typename options_t>
    inline void parse_with_size(options_t& options) {
        if constexpr (has_with_size<options_t>) {
            options.with_size = false;
        }
    }

    template <typename instream, typename outstream, typename options_t>
    inline void do_decode(instream& input, outstream& output, options_t const& options) {
        auto const do_print_end = [&]() noexcept {
            if constexpr (has_print_end<options_t>) {
                boost::io::ios_all_saver const flags(std::cout);
                std::cout << "0x" << std::hex << std::setw(6) << std::setfill('0')
                          << std::uppercase << std::right << input.tellg() << std::endl;
            }
        };
        if constexpr (has_moduled<options_t>) {
            if (options.moduled) {
                std::apply(
                        OVERLOADS_OF(options_t::format_t::moduled_decode),
                        get_moduled_decode_args(options, input, output));
                do_print_end();
                return;
            }
        }
        std::apply(
                OVERLOADS_OF(options_t::format_t::decode),
                get_decode_args(options, input, output));
        do_print_end();
    }

    template <typename instream, typename outstream, typename options_t>
    inline void do_encode(instream& input, outstream& output, options_t const& options) {
        if constexpr (has_moduled<options_t>) {
            if (options.moduled) {
                std::apply(
                        OVERLOADS_OF(options_t::format_t::moduled_encode),
                        get_moduled_encode_args(options, input, output));
                return;
            }
        }
        std::apply(
                OVERLOADS_OF(options_t::format_t::encode),
                get_encode_args(options, input, output));
    }

    template <typename options_t>
    inline int crunch_file(
            std::filesystem::path& infile, std::filesystem::path& outfile,
            options_t const& options) {
        std::ifstream input(infile, std::ios::in | std::ios::binary);
        if (!input.good()) {
            std::cerr << "Input file '" << infile << "' could not be opened.\n\n";
            return 2;
        }
        std::stringstream buffer(std::ios::in | std::ios::out | std::ios::binary);

        if constexpr (has_pointer<options_t>) {
            input.seekg(options.pointer);
        }
        detail::do_decode(input, buffer, options);
        input.close();
        buffer.seekg(0);
        std::fstream output(
                outfile,
                std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output.good()) {
            std::cerr << "Output file '" << outfile << "' could not be opened.\n\n";
            return 3;
        }
        detail::do_encode(buffer, output, options);
        return 0;
    }

    template <typename options_t>
    inline int decode_file(
            std::filesystem::path& infile, std::filesystem::path& outfile,
            options_t const& options) {
        std::ifstream input(infile, std::ios::in | std::ios::binary);
        if (!input.good()) {
            std::cerr << "Input file '" << infile << "' could not be opened.\n\n";
            return 2;
        }
        std::fstream output(
                outfile,
                std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output.good()) {
            std::cerr << "Output file '" << outfile << "' could not be opened.\n\n";
            return 3;
        }
        if constexpr (has_pointer<options_t>) {
            input.seekg(options.pointer);
        }
        detail::do_decode(input, output, options);
        return 0;
    }

    template <typename options_t>
    inline int encode_file(
            std::filesystem::path& infile, std::filesystem::path& outfile,
            options_t const& options) {
        std::ifstream input(infile, std::ios::in | std::ios::binary);
        if (!input.good()) {
            std::cerr << "Input file '" << infile << "' could not be opened.\n\n";
            return 2;
        }
        std::fstream output(
                outfile,
                std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output.good()) {
            std::cerr << "Output file '" << outfile << "' could not be opened.\n\n";
            return 3;
        }
        detail::do_encode(input, output, options);
        return 0;
    }

    template <typename options_t>
    inline void command_argument_parser(options_t& options) {
        options.program = options.arguments.front();
        int const count = static_cast<int>(std::ssize(options.arguments));
        while (true) {
            int       option_index = 0;
            int const option_char  = getopt_long(
                    count, options.arguments.data(), options_t::short_options.data(),
                    options_t::long_options.data(), &option_index);
            if (option_char == -1) {
                break;
            }

            switch (option_char) {
            case 'x':
                parse_extract(options, optarg);
                break;
            case 'c':
                parse_crunch(options);
                break;
            case 'm':
                parse_moduled(options);
                break;
            case 'p':
                parse_padding(options, optarg);
                break;
            case 'i':
                parse_print_end(options);
                break;
            case 's':
                parse_size(options, optarg);
                break;
            case 'S':
                parse_with_size(options);
                break;
            default:
                break;
            }
        }
        options.positional = options.arguments.subspan(static_cast<size_t>(optind));
    }

    template <typename options_t>
    int print_usage(options_t const& options, std::ostream& out) {
        using namespace std::string_view_literals;
        auto const [moduled_opt, moduled_arg] = [&]() {
            if constexpr (has_moduled<options_t>) {
                return std::pair{
                        " [-m|--moduled]"sv,
                        std::array{
                                   "        -m,--moduled    Compress {output_filename} into 4096-byte modules of chosen format.\n"sv,
                                   "        -m,--moduled    Decompress {input_filename} as 4096-byte modules of chosen format.\n"sv,
                                   "        -m,--moduled    Recompress {input_filename} into 4096-byte modules of chosen format.\n"sv,
                                   }
                };
            } else {
                return std::pair{
                        ""sv, std::array{""sv, ""sv, ""sv}
                };
            }
        }();
        auto const [padding_opt, padding_arg] = [&]() {
            if constexpr (has_padding<options_t>) {
                return std::pair{
                        " [-p|--padding={size}]"sv,
                        "        -p,--padding    Requires -m|--moduled. Pads modules to multiples of {size}, which must be a power of 2.\n"sv};
            } else {
                return std::pair{""sv, ""sv};
            }
        }();
        auto const [info_opt, info_arg] = [&]() {
            if constexpr (has_print_end<options_t>) {
                return std::pair{
                        " [-i|--info]"sv,
                        "        -i|--info       Print out the position where the compressed data ends.\n"sv};
            } else {
                return std::pair{""sv, ""sv};
            }
        }();
        auto const [size_opt, size_arg] = [&]() {
            if constexpr (has_size<options_t>) {
                return std::pair{
                        " [-s|--size={size}]"sv,
                        "        -s,--size       Use {size} as the decompressed file size of {input_filename}.\n"sv};
            } else {
                return std::pair{""sv, ""sv};
            }
        }();
        auto const [with_size_opt, with_size_arg] = [&]() {
            if constexpr (has_with_size<options_t>) {
                return std::pair{
                        " [-S|--no-size]"sv,
                        "        -S,--no-size    {output_filename} will not have decompressed file size.\n"sv};
            } else {
                return std::pair{""sv, ""sv};
            }
        }();
        auto const program = options.program.filename().string();
        out << "Usage: " << program;
        out << moduled_opt;
        out << padding_opt;
        out << size_opt;
        out << " {input_filename} {output_filename}\n"sv;
        out << "    Compresses {input_filename} into {output_filename}.\n"sv;
        out << moduled_arg[0];
        out << padding_arg;
        out << size_arg;
        out << "\nUsage: " << program << " -x|--extract[={pointer}]"sv;
        out << info_opt;
        out << moduled_opt;
        out << padding_opt;
        out << with_size_opt;
        out << " {input_filename} {output_filename}\n"sv;
        out << "    Decompresses {input_filename} into {output_filename}.\n"sv;
        out << "    If given, {pointer} is the offset into the file to decompress from.\n"sv;
        out << info_arg;
        out << moduled_arg[1];
        out << padding_arg;
        out << with_size_arg;
        if constexpr (has_crunch<options_t>) {
            out << "\nUsage: " << program << " -c|--crunch"sv;
            out << moduled_opt;
            out << padding_opt;
            out << size_opt;
            out << with_size_opt;
            out << " {input_filename} [{output_filename}]\n"sv;
            out << "    Decompresses {input_filename} and recompresses it to {output_filename}\n"sv;
            out << "    If {output_filename} is missing, {input_filename} is used as {output_filename}.\n"sv;
            out << moduled_arg[2];
            out << padding_arg;
            out << size_arg;
            out << with_size_arg;
        }

        return 1;
    }
}    // namespace detail

#undef FWD
#undef RETURNS
#undef OVERLOADS_OF

template <typename options_t>
inline int auto_compressor_decompressor(options_t options) {
    try {
        detail::command_argument_parser(options);
        if constexpr (detail::has_crunch<options_t>) {
            if (options.positional.size() != 2
                && (!options.crunch || options.positional.size() != 1)) {
                detail::print_usage(options, std::cout);
                return 1;
            }
            if (options.extract && options.crunch) {
                std::cerr << "Error: --extract and --crunch can't be used at the "
                             "same time.\n\n";
                return 4;
            }
        } else {
            if (options.positional.size() != 2) {
                detail::print_usage(options, std::cout);
                return 1;
            }
        }

        std::filesystem::path infile{options.positional.front()};
        std::filesystem::path outfile{options.positional.back()};

        if constexpr (detail::has_crunch<options_t>) {
            if (options.crunch) {
                return detail::crunch_file(infile, outfile, options);
            }
        }

        if (options.extract) {
            return detail::decode_file(infile, outfile, options);
        }

        return detail::encode_file(infile, outfile, options);
    } catch (int error) {
        return error;
    } catch (...) {
        return -1;
    }
}

#endif    // LIB_OPTIONS_LIB_HH
