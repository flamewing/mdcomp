/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * Copyright (C) Flamewing 2013-2016 <flamewing.sonic@gmail.com>
 * Very loosely based on code by the KENS Project Development Team
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

#ifndef LIB_SAXMAN_HH
#define LIB_SAXMAN_HH

#include <mdcomp/basic_decoder.hh>
#include <mdcomp/moduled_adaptor.hh>

#include <iosfwd>


class saxman;
using basic_saxman   = BasicDecoder<saxman, PadMode::DontPad, bool>;
using moduled_saxman = ModuledAdaptor<saxman, 4096U, 1U>;

class saxman : public basic_saxman, public moduled_saxman {
    friend basic_saxman;
    friend moduled_saxman;
    static bool
            encode(std::ostream& Dst, uint8_t const* data, size_t Size,
                   bool WithSize = true);

public:
    using basic_saxman::encode;
    static bool decode(std::istream& Src, std::iostream& Dst, size_t Size = 0);
};

#endif    // LIB_SAXMAN_HH
