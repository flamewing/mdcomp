/*
 * Copyright (C) Flamewing 2016 <flamewing.sonic@gmail.com>
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
#include <memory>
#include <ranges>
#include <sstream>
#include <vector>

template <typename Format, size_t DefaultModuleSize, size_t DefaultModulePadding>
class moduled_adaptor {
public:
    enum {
        MODULE_SIZE    = DefaultModuleSize,
        MODULE_PADDING = DefaultModulePadding
    };

    static size_t pad_mask_bits;

    static bool moduled_decode(
            std::istream& source, std::iostream& dest,
            size_t module_padding = DefaultModulePadding);

    static bool moduled_encode(
            std::istream& source, std::ostream& dest,
            size_t module_padding = DefaultModulePadding);
};

template <typename Format, size_t DefaultModuleSize, size_t DefaultModulePadding>
bool moduled_adaptor<Format, DefaultModuleSize, DefaultModulePadding>::moduled_decode(
        std::istream& source, std::iostream& dest, size_t const module_padding) {
    int64_t const     full_size = big_endian::read2(source);
    std::stringstream input(std::ios::in | std::ios::out | std::ios::binary);
    input << source.rdbuf();
    detail::pad_to_even(input);
    input.seekg(0);

    auto const padding = static_cast<std::streamoff>(module_padding);
    while (true) {
        Format::decode(input, dest);
        if (dest.tellp() >= full_size) {
            break;
        }

        // Skip padding between modules
        input.seekg(detail::round_up(input.tellg(), padding));
    }

    return true;
}

template <typename Format, size_t DefaultModuleSize, size_t DefaultModulePadding>
bool moduled_adaptor<Format, DefaultModuleSize, DefaultModulePadding>::moduled_encode(
        std::istream& source, std::ostream& dest, size_t const module_padding) {
    auto location = source.tellg();
    source.ignore(std::numeric_limits<std::streamsize>::max());
    auto full_size = source.gcount();
    source.seekg(location);
    std::vector<uint8_t> data;
    data.resize(static_cast<size_t>(full_size));
    auto pointer = std::ranges::cbegin(data);
    source.read(reinterpret_cast<char*>(data.data()), full_size);

    big_endian::write2(
            dest, static_cast<size_t>(full_size) & std::numeric_limits<uint16_t>::max());
    std::stringstream buffer(std::ios::in | std::ios::out | std::ios::binary);

    auto const padding = static_cast<std::streamoff>(module_padding);
    while (full_size > MODULE_SIZE) {
        // We want to manage internal padding for all modules but the last.
        pad_mask_bits = 8 * module_padding - 1U;
        Format::encode(buffer, {std::to_address(pointer), MODULE_SIZE});
        full_size -= MODULE_SIZE;
        pointer += MODULE_SIZE;
        // Padding between modules
        detail::pad_to_multiple(buffer, padding);
    }

    pad_mask_bits = 7U;
    Format::encode(buffer, {std::to_address(pointer), static_cast<size_t>(full_size)});

    // Pad to even size.
    dest << buffer.rdbuf();
    detail::pad_to_even(dest);
    return true;
}

#endif    // LIB_MODULED_ADAPTOR_HH
