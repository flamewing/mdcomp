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

#ifndef __LIB_KOSINSKI_H
#define __LIB_KOSINSKI_H

#include <iosfwd>
#include "basic_decoder.h"
#include "moduled_adaptor.h"

class kosinski;
typedef BasicDecoder<kosinski, false> basic_kosinski;
typedef ModuledAdaptor<kosinski, 4096u, 16u> moduled_kosinski;

class kosinski : public basic_kosinski, public moduled_kosinski {
public:
	using basic_kosinski::encode;
	static bool decode(std::istream &Src, std::iostream &Dst);
	static bool encode(std::ostream &Dst, unsigned char const *data, size_t const Size);
};

#endif // __LIB_KOSINSKI_H
