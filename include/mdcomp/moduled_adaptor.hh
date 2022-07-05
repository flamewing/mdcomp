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

#include <ios>
#include <limits>
#include <memory>
#include <sstream>
#include <vector>

namespace detail {
    template <typename T1, typename T2>

        requires requires(T1 value1, T2 value2) {
            value1 + value2;
            value1 - value2;
            value1* value2;
            value1 / value2;
            value1 % value2;
        }

    constexpr inline auto round_up(T1 const value, T2 const factor) noexcept {
        constexpr decltype(factor) const one{1};
        return ((value + factor - one) / factor) * factor;
    };
}    // namespace detail

template <typename Format, size_t DefaultModuleSize, size_t DefaultModulePadding>
class ModuledAdaptor {
public:
    enum {
        ModuleSize    = DefaultModuleSize,
        ModulePadding = DefaultModulePadding
    };

    static size_t PadMaskBits;
    static bool   moduled_decode(
              std::istream& Source, std::iostream& Dest,
              size_t ModulePadding = DefaultModulePadding);

    static bool moduled_encode(
            std::istream& Source, std::ostream& Dest,
            size_t ModulePadding = DefaultModulePadding);
};

template <typename Format, size_t DefaultModuleSize, size_t DefaultModulePadding>
bool ModuledAdaptor<Format, DefaultModuleSize, DefaultModulePadding>::moduled_decode(
        std::istream& Source, std::iostream& Dest, size_t const ModulePadding) {
    int64_t const     FullSize = BigEndian::Read2(Source);
    std::stringstream input(std::ios::in | std::ios::out | std::ios::binary);
    input << Source.rdbuf();

    // Pad to even length, for safety.
    if ((input.tellp() % 2) != 0) {
        input.put(0);
    }

    input.seekg(0);

    auto const padding = static_cast<std::streamoff>(ModulePadding);

    while (true) {
        Format::decode(input, Dest);
        if (Dest.tellp() >= FullSize) {
            break;
        }

        // Skip padding between modules
        input.seekg(detail::round_up(input.tellg(), padding));
    }

    return true;
}

template <typename Format, size_t DefaultModuleSize, size_t DefaultModulePadding>
bool ModuledAdaptor<Format, DefaultModuleSize, DefaultModulePadding>::moduled_encode(
        std::istream& Source, std::ostream& Dest, size_t const ModulePadding) {
    auto Location = Source.tellg();
    Source.ignore(std::numeric_limits<std::streamsize>::max());
    auto FullSize = Source.gcount();
    Source.seekg(Location);
    std::vector<uint8_t> data;
    data.resize(static_cast<size_t>(FullSize));
    auto pointer = data.cbegin();
    Source.read(reinterpret_cast<char*>(data.data()), FullSize);

    auto const padding = static_cast<std::streamoff>(ModulePadding);

    BigEndian::Write2(
            Dest, static_cast<size_t>(FullSize) & std::numeric_limits<uint16_t>::max());
    std::stringstream buffer(std::ios::in | std::ios::out | std::ios::binary);

    while (FullSize > ModuleSize) {
        // We want to manage internal padding for all modules but the last.
        PadMaskBits = 8 * ModulePadding - 1U;
        Format::encode(buffer, std::to_address(pointer), ModuleSize);
        FullSize -= ModuleSize;
        pointer += ModuleSize;

        // Padding between modules
        int64_t const paddingEnd = detail::round_up(buffer.tellp(), padding);
        while (buffer.tellp() < paddingEnd) {
            buffer.put(0);
        }
    }

    PadMaskBits = 7U;
    Format::encode(buffer, std::to_address(pointer), static_cast<size_t>(FullSize));

    // Pad to even size.
    Dest << buffer.rdbuf();
    if ((Dest.tellp() % 2) != 0) {
        Dest.put(0);
    }
    return true;
}

#endif    // LIB_MODULED_ADAPTOR_HH
