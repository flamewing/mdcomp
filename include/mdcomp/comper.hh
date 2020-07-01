/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
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

#ifndef LIB_COMPER_HH
#define LIB_COMPER_HH

#include <mdcomp/basic_decoder.hh>
#include <mdcomp/moduled_adaptor.hh>

#include <iosfwd>


class comper;
using basic_comper   = BasicDecoder<comper, PadMode::PadEven>;
using moduled_comper = ModuledAdaptor<comper, 4096U, 1U>;

class comper : public basic_comper, public moduled_comper {
    friend basic_comper;
    friend moduled_comper;
    static bool encode(std::ostream& Dst, uint8_t const* data, size_t Size);

public:
    using basic_comper::encode;
    static bool decode(std::istream& Src, std::iostream& Dst);
};

#endif    // LIB_COMPER_HH
