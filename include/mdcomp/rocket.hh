/*
 * Copyright (C) Clownacy 2016
 * Copyright (C) Flamewing 2016 <flamewing.sonic@gmail.com>
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

#ifndef LIB_ROCKET_HH
#define LIB_ROCKET_HH

#include "mdcomp/basic_decoder.hh"
#include "mdcomp/moduled_adaptor.hh"

#include <iosfwd>

class rocket;
using basic_rocket   = BasicDecoder<rocket, PadMode::DontPad>;
using moduled_rocket = ModuledAdaptor<rocket, 4096U, 1U>;

class rocket : public basic_rocket, public moduled_rocket {
    friend basic_rocket;
    friend moduled_rocket;
    static bool encode(std::ostream& Dest, uint8_t const* data, size_t Size);

public:
    static bool encode(std::istream& Source, std::ostream& Dest);
    static bool decode(std::istream& Source, std::iostream& Dest);
};

#endif    // LIB_ROCKET_HH
