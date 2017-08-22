/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
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

#include <cstdint>
#include <iostream>
#include <istream>
#include <ostream>

#include "artc42.h"
#include "bigendian_io.h"
#include "bitstream.h"
#include "ignore_unused_variable_warning.h"

using namespace std;

using EniIBitstream = ibitstream<uint16_t, true>;
using EniOBitstream = obitstream<uint16_t>;

class artc42_internal {
public:
	static void decode(std::istream &Src, std::ostream &Dst) {
		ignore_unused_variable_warning(Src, Dst);
	}

	static void encode(std::istream &Src, std::ostream &Dst) {
		ignore_unused_variable_warning(Src, Dst);
	}
};

bool artc42::decode(istream &Src, ostream &Dst) {
	ignore_unused_variable_warning(Src, Dst);
	return false;
}

bool artc42::encode(istream &Src, ostream &Dst) {
	ignore_unused_variable_warning(Src, Dst);
	return false;
}
