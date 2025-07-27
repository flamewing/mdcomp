/*
 * Copyright (C) Flamewing 2022-2025 <flamewing.sonic@gmail.com>
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

#include <array>
#include <bit>
#include <charconv>
#include <concepts>    // IWYU pragma: keep
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iostream>
#include <memory>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

enum class argument : uint8_t {
    none     = no_argument,          // option never takes an argument
    required = required_argument,    // option always requires an argument
    optional = optional_argument     // option may take an argument
};

struct option_t : option {
    constexpr option_t(char const* m_name, argument m_has_arg, int m_val)
            : option{m_name, static_cast<int>(m_has_arg), nullptr, m_val} {}

    constexpr option_t() : option{} {}
};

template <auto& long_options>
requires std::ranges::range<decltype(long_options)>
consteval auto make_short_options() {
    static_assert(long_options.back().name == nullptr);
    constexpr auto const result = [&]() consteval noexcept {
        std::array<char, 3U * (long_options.size() - 1U)> intermediate{};

        size_t length = 0;
        for (auto const& opt : long_options) {
            if (opt.name == nullptr) {
                break;
            }
            auto const val = static_cast<char>(opt.val);
            if (val == '\0') {
                // Allow options without a short form.
                continue;
            }
            intermediate[length++] = val;
            // NOLINTNEXTLINE(clang-diagnostic-switch-enum)
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
            default:
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
            std::cerr << std::format(
                    "Invalid value '{}' given for '{}' parameter!\n", value, parameter);
        } else if (error == std::errc::result_out_of_range) {
            std::cerr << std::format(
                    "The value '{}' given for '{}' parameter is out of range!\n", value,
                    parameter);
        } else {
            std::cerr << std::format(
                    "Unknown error happened when parsing value '{}' given for '{}' "
                    "parameter!\n",
                    value, parameter);
        }
        throw 5;
    }

    template <std::integral T>
    inline void read_value(std::string_view parameter, T& value) {
        bool const starts_with_minus = parameter.starts_with('-');
        if (starts_with_minus || parameter.starts_with('+')) {
            parameter.remove_prefix(1);
        }

        if constexpr (std::is_unsigned_v<T>) {
            if (starts_with_minus) {
                std::cerr << "Cannot parse negative value for unsigned type!\n";
                throw 5;
            }
        }
        int const base = [&]() {
            if (parameter.starts_with("0x") || parameter.starts_with("0X")) {
                parameter.remove_prefix(2);
                return 16;
            }
            if (parameter.starts_with("0b") || parameter.starts_with("0B")) {
                parameter.remove_prefix(2);
                return 2;
            }
            if (parameter.starts_with("0o") || parameter.starts_with("0O")) {
                parameter.remove_prefix(2);
                return 8;
            }
            return 10;
        }();
        auto const* const start = std::to_address(std::ranges::cbegin(parameter));
        auto const* const end   = std::to_address(std::ranges::cend(parameter));
        if (auto [ptr, ec] = std::from_chars(start, end, value, base);
            ec != std::errc{} || ptr != end) {
            print_error(ec, "value", parameter.data());
        }
        if constexpr (std::is_signed_v<T>) {
            if (starts_with_minus) {
                value = -value;
            }
        }
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
            read_value({parameter_in}, options.pointer);
        }
        if (options.pointer < 0) {
            std::cerr << "Error: specified file offset must be a positive number.\n";
            throw 4;
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
                read_value({parameter_in}, options.padding);
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
    inline void parse_size(options_t& options, char const* parameter_in) {
        if constexpr (has_size<options_t>) {
            if (parameter_in != nullptr) {
                read_value({parameter_in}, options.size);
            }
            if (options.size == 0) {
                std::cerr << "Error: specified size must be a positive number.\n";
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
        auto const do_print_end = [&]() {
            if constexpr (has_print_end<options_t>) {
                std::cout << std::format("0x{:06x}", static_cast<size_t>(input.tellg()));
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
            std::filesystem::path const& infile, std::filesystem::path const& outfile,
            options_t const& options) {
        std::ifstream input(infile, std::ios::in | std::ios::binary);
        if (!input.good()) {
            std::cerr << std::format(
                    "Input file '{}' could not be opened.\n", infile.string());
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
            std::cerr << std::format(
                    "Output file '{}' could not be opened.\n", outfile.string());
            return 3;
        }
        detail::do_encode(buffer, output, options);
        return 0;
    }

    template <typename options_t>
    inline int decode_file(
            std::filesystem::path const& infile, std::filesystem::path const& outfile,
            options_t const& options) {
        std::ifstream input(infile, std::ios::in | std::ios::binary);
        if (!input.good()) {
            std::cerr << std::format(
                    "Input file '{}' could not be opened.\n", infile.string());
            return 2;
        }
        std::fstream output(
                outfile,
                std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output.good()) {
            std::cerr << std::format(
                    "Output file '{}' could not be opened.\n", outfile.string());
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
            std::filesystem::path const& infile, std::filesystem::path const& outfile,
            options_t const& options) {
        std::ifstream input(infile, std::ios::in | std::ios::binary);
        if (!input.good()) {
            std::cerr << std::format(
                    "Input file '{}' could not be opened.\n", infile.string());
            return 2;
        }
        std::fstream output(
                outfile,
                std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output.good()) {
            std::cerr << std::format(
                    "Output file '{}' could not be opened.\n", outfile.string());
            return 3;
        }
        detail::do_encode(input, output, options);
        return 0;
    }

    template <typename options_t>
    inline void command_argument_parser(options_t& options) {
        options.program  = options.arguments.front();
        auto const count = static_cast<int>(std::ssize(options.arguments));
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
        out << std::format("Usage: {}", program);
        out << moduled_opt;
        out << padding_opt;
        out << size_opt;
        out << " {input_filename} {output_filename}\n"sv;
        out << "    Compresses {input_filename} into {output_filename}.\n"sv;
        out << moduled_arg[0];
        out << padding_arg;
        out << size_arg;
        out << std::format("\nUsage: {} -x|--extract[={{pointer}}]", program);
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
            out << std::format("\nUsage: {} -c|--crunch", program);
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
                             "same time.\n";
                return 4;
            }
        } else {
            if (options.positional.size() != 2) {
                detail::print_usage(options, std::cout);
                return 1;
            }
        }

        constexpr static auto as_u8string_view = [](std::string_view path) {
            return std::u8string_view{
                    reinterpret_cast<char8_t const*>(path.data()), path.size()};
        };
        std::filesystem::path infile{as_u8string_view(options.positional.front())};
        std::filesystem::path outfile{as_u8string_view(options.positional.back())};

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
