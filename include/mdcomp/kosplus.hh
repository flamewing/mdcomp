/*
 * Copyright (C) Flamewing 2015-2016 <flamewing.sonic@gmail.com>
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

#ifndef LIB_KOSPLUS_HH
#define LIB_KOSPLUS_HH

#include "mdcomp/basic_decoder.hh"
#include "mdcomp/moduled_adaptor.hh"

#include <iosfwd>

class kosplus;
using basic_kosplus   = basic_decoder<kosplus, pad_mode::dont_pad>;
using moduled_kosplus = moduled_adaptor<kosplus, 4096U, 1U>;

class kosplus : public basic_kosplus, public moduled_kosplus {
    friend basic_kosplus;
    friend moduled_kosplus;
    static bool encode(std::ostream& dest, std::span<uint8_t const> data);

public:
    using basic_kosplus::encode;
    static bool decode(std::istream& source, std::iostream& dest);
};

#endif    // LIB_KOSPLUS_HH
