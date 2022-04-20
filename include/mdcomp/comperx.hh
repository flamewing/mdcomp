/*
 * Copyright (C) Flamewing 2013-2016 <flamewing.sonic@gmail.com>
 *
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

#ifndef LIB_COMPERX_HH
#define LIB_COMPERX_HH

#include "mdcomp/basic_decoder.hh"
#include "mdcomp/moduled_adaptor.hh"

#include <iosfwd>

class comperx;
using basic_comperx   = BasicDecoder<comperx, PadMode::PadEven>;
using moduled_comperx = ModuledAdaptor<comperx, 4096U, 1U>;

class comperx : public basic_comperx, public moduled_comperx {
    friend basic_comperx;
    friend moduled_comperx;
    static bool encode(std::ostream& Dst, uint8_t const* data, size_t Size);

public:
    using basic_comperx::encode;
    static bool decode(std::istream& Src, std::iostream& Dst);
};

#endif    // LIB_COMPERX_HH
