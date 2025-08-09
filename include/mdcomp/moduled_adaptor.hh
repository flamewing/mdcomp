/*
 * Copyright (C) Flamewing 2016-2025 <flamewing.sonic@gmail.com>
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with main.c; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor Boston, MA 02110-1301,  USA
 */

#ifndef LIB_MODULED_ADAPTOR_HH
#define LIB_MODULED_ADAPTOR_HH

#include "mdcomp/bigendian_io.hh"
#include "mdcomp/stream_utils.hh"

#include <cstddef>
#include <cstdint>
#include <ios>
#include <iosfwd>
#include <limits>
#include <sstream>
#include <type_traits>
#include <vector>

template <typename Format, size_t ModuleSize, size_t DefaultModulePadding>
class moduled_adaptor {
    moduled_adaptor() = default;
    friend Format;

public:
    constexpr static size_t default_module_padding = DefaultModulePadding;

    static bool moduled_decode(
            std::istream& source, std::iostream& dest,
            size_t module_padding = default_module_padding);

    static bool moduled_encode(
            std::istream& source, std::ostream& dest,
            size_t module_padding = default_module_padding);
};

template <typename Format, size_t ModuleSize, size_t DefaultModulePadding>
bool moduled_adaptor<Format, ModuleSize, DefaultModulePadding>::moduled_decode(
        std::istream& source, std::iostream& dest, size_t const module_padding) {
    using diff_t                = std::make_signed_t<size_t>;
    int64_t const     full_size = big_endian::read2(source);
    std::stringstream input(std::ios::in | std::ios::out | std::ios::binary);
    input << source.rdbuf();
    detail::pad_to_even(input);
    input.seekg(0);

    while (true) {
        Format::decode(input, dest);
        if (dest.tellp() >= full_size) {
            break;
        }

        // Skip padding between modules
        input.seekg(detail::round_up(input.tellg(), static_cast<diff_t>(module_padding)));
    }

    return true;
}

template <typename Format, size_t ModuleSize, size_t DefaultModulePadding>
bool moduled_adaptor<Format, ModuleSize, DefaultModulePadding>::moduled_encode(
        std::istream& source, std::ostream& dest, size_t const module_padding) {
    using diff_t  = std::make_signed_t<size_t>;
    auto location = source.tellg();
    source.ignore(std::numeric_limits<std::streamsize>::max());
    auto full_size = static_cast<size_t>(source.gcount());
    source.seekg(location);
    auto data = detail::read_from_bytes<std::vector<char>>(source, full_size);

    big_endian::write2(dest, full_size & std::numeric_limits<uint16_t>::max());
    std::stringstream buffer(std::ios::in | std::ios::out | std::ios::binary);
    std::span         input_span(data);
    while (input_span.size() > ModuleSize) {
        Format::encode(buffer, input_span.subspan(0, ModuleSize));
        input_span = input_span.subspan(ModuleSize);
        // Padding between modules
        detail::pad_to_multiple(buffer, static_cast<diff_t>(module_padding));
    }

    Format::encode(buffer, input_span);

    // Pad to even size.
    dest << buffer.rdbuf();
    detail::pad_to_even(dest);
    return true;
}

#endif    // LIB_MODULED_ADAPTOR_HH
