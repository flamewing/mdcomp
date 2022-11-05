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

#ifndef LIB_LZKN1_HH
#define LIB_LZKN1_HH

#include "mdcomp/basic_decoder.hh"
#include "mdcomp/moduled_adaptor.hh"

#include <iosfwd>

class lzkn1;
using basic_lzkn1   = basic_decoder<lzkn1, pad_mode::dont_pad>;
using moduled_lzkn1 = moduled_adaptor<lzkn1, 4096U, 1U>;

class lzkn1 : public basic_lzkn1, public moduled_lzkn1 {
    friend basic_lzkn1;
    friend moduled_lzkn1;
    static bool encode(std::ostream& dest, std::span<uint8_t const> data);

public:
    using basic_lzkn1::encode;
    static bool decode(std::istream& source, std::iostream& dest);
};

#endif    // LIB_LZKN1_HH
