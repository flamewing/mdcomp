/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
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

#ifndef __LIB_BASIC_DECODER_H
#define __LIB_BASIC_DECODER_H

#include <mdcomp/bigendian_io.hh>

#include <iosfwd>
#include <limits>
#include <vector>


enum class PadMode { DontPad, PadEven };

template <typename Format, PadMode Pad, typename... Args>
class BasicDecoder {
public:
    static bool encode(std::istream& Src, std::ostream& Dst, Args... args);
    static void extract(std::istream& Src, std::iostream& Dst);
};

template <typename Format, PadMode Pad, typename... Args>
bool BasicDecoder<Format, Pad, Args...>::encode(
        std::istream& Src, std::ostream& Dst, Args... args) {
    size_t Start = Src.tellg();
    Src.ignore(std::numeric_limits<std::streamsize>::max());
    size_t FullSize = Src.gcount();
    Src.seekg(Start);
    std::vector<uint8_t> data;
    if (Pad == PadMode::PadEven) {
        data.resize(FullSize + (FullSize & 1));
    } else {
        data.resize(FullSize);
    }
    Src.read(reinterpret_cast<char*>(&(data.front())), data.size());
    if (Pad == PadMode::PadEven && data.size() > FullSize) {
        data.back() = 0;
    }
    if (Format::encode(
                Dst, data.data(), data.size(), std::forward<Args>(args)...)) {
        // Pad to even size.
        if ((Dst.tellp() & 1) != 0) {
            Dst.put(0);
        }
        return true;
    }
    return false;
}

template <typename Format, PadMode Pad, typename... Args>
void BasicDecoder<Format, Pad, Args...>::extract(
        std::istream& Src, std::iostream& Dst) {
    Dst << Src.rdbuf();
    // Pad to even size.
    if ((Dst.tellp() & 1) != 0) {
        Dst.put(0);
    }
    Dst.seekg(0);
}

#endif    // __LIB_MODULED_ADAPTOR_H
