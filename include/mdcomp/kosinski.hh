/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * Copyright (C) Flamewing 2011-2016 <flamewing.sonic@gmail.com>
 * Copyright (C) 2002-2004 The KENS Project Development Team
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

#ifndef LIB_KOSINSKI_HH
#define LIB_KOSINSKI_HH

#include <mdcomp/basic_decoder.hh>
#include <mdcomp/moduled_adaptor.hh>

#include <iosfwd>


class kosinski;
using basic_kosinski   = BasicDecoder<kosinski, PadMode::DontPad>;
using moduled_kosinski = ModuledAdaptor<kosinski, 4096U, 16U>;

class kosinski : public basic_kosinski, public moduled_kosinski {
    friend basic_kosinski;
    friend moduled_kosinski;
    static bool encode(std::ostream& Dst, uint8_t const* data, size_t Size);

public:
    using basic_kosinski::encode;
    static bool decode(std::istream& Src, std::iostream& Dst);
};

#endif    // LIB_KOSINSKI_HH
