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

#ifndef LIB_BASIC_DECODER_H
#define LIB_BASIC_DECODER_H

#include "mdcomp/bigendian_io.hh"

#include <iostream>
#include <limits>
#include <vector>

enum class pad_mode {
    dont_pad,
    pad_even
};

template <typename Format, pad_mode Pad, typename... Args>
class basic_decoder {
public:
    static bool encode(std::istream& source, std::ostream& dest, Args... args);
    static void extract(std::istream& source, std::iostream& dest);
};

template <typename Format, pad_mode Pad, typename... Args>
bool basic_decoder<Format, Pad, Args...>::encode(
        std::istream& source, std::ostream& dest, Args... args) {
    auto start = source.tellg();
    source.ignore(std::numeric_limits<std::streamsize>::max());
    auto full_size = static_cast<size_t>(source.gcount());
    source.seekg(start);
    std::vector<uint8_t> data;
    if (Pad == pad_mode::pad_even) {
        data.resize(full_size + (full_size % 2));
    } else {
        data.resize(full_size);
    }
    source.read(reinterpret_cast<char*>(data.data()), std::ssize(data));
    if (Pad == pad_mode::pad_even && data.size() > full_size) {
        data.back() = 0;
    }
    if (Format::encode(dest, data.data(), data.size(), std::forward<Args>(args)...)) {
        // Pad to even size.
        if ((dest.tellp() % 2) != 0) {
            dest.put(0);
        }
        return true;
    }
    return false;
}

template <typename Format, pad_mode Pad, typename... Args>
void basic_decoder<Format, Pad, Args...>::extract(
        std::istream& source, std::iostream& dest) {
    dest << source.rdbuf();
    // Pad to even size.
    if ((dest.tellp() % 2) != 0) {
        dest.put(0);
    }
    dest.seekg(0);
}

#endif    // LIB_MODULED_ADAPTOR_H
