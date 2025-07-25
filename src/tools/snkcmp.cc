/*
 * Copyright (C) Flamewing 2017-2025 <flamewing.sonic@gmail.com>
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

#include "mdcomp/forge_span.hh"
#include "mdcomp/options_lib.hh"
#include "mdcomp/snkrle.hh"

#include <array>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <span>

struct options_t {
    explicit options_t(std::span<char*> args) : arguments(args) {}

    constexpr static std::array const long_options{
            option_t{"extract", argument::optional, 'x'},
            option_t{"crunch", argument::none, 'c'},
            option_t{}
    };
    constexpr static auto short_options = make_short_options<long_options>();

    std::filesystem::path program;
    std::span<char*>      arguments;
    std::span<char*>      positional;
    std::streamsize       pointer = 0;

    bool extract = false;
    bool crunch  = false;

    using format_t = snkrle;
};

int main(int argc, char* argv[]) {
    return auto_compressor_decompressor(options_t(unsafe_forge_span(argv, argc)));
}
