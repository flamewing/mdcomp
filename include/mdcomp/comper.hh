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

#include "mdcomp/basic_decoder.hh"
#include "mdcomp/moduled_adaptor.hh"

#include <iosfwd>

class comper;
using basic_comper   = basic_decoder<comper, pad_mode::pad_even>;
using moduled_comper = moduled_adaptor<comper, 4096U, 1U>;

class comper : public basic_comper, public moduled_comper {
    friend basic_comper;
    friend moduled_comper;
    static bool encode(std::ostream& dest, std::span<uint8_t const> data);

public:
    using basic_comper::encode;
    static bool decode(std::istream& source, std::iostream& dest);
};

#endif    // LIB_COMPER_HH
