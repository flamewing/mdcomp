/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * Copyright (C) Flamewing 2016 <flamewing.sonic@gmail.com>
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with main.c; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor Boston, MA 02110-1301,  USA
 */

#ifndef __LIB_MODULED_ADAPTOR_H
#define __LIB_MODULED_ADAPTOR_H

#include <sstream>
#include <vector>
#include "bigendian_io.h"

template <typename Format, size_t DefaultModuleSize, size_t DefaultModulePadding>
class ModuledAdaptor {
public:
	enum {
		ModuleSize = DefaultModuleSize,
		ModulePadding = DefaultModulePadding
	};
	static size_t PadMaskBits;
	static bool moduled_decode(std::istream &Src, std::iostream &Dst,
	                           size_t const ModulePadding = DefaultModulePadding);

	static bool moduled_encode(std::istream &Src, std::ostream &Dst,
	                           size_t ModuleSize = DefaultModuleSize,
	                           size_t const ModulePadding = DefaultModulePadding);
};

template <typename Format, size_t DefaultModuleSize, size_t DefaultModulePadding>
bool ModuledAdaptor<Format, DefaultModuleSize, DefaultModulePadding>::moduled_decode(
	std::istream &Src,
	std::iostream &Dst,
	size_t const ModulePadding
) {
	long long FullSize = BigEndian::Read2(Src);
	std::stringstream in(std::ios::in | std::ios::out | std::ios::binary);
	in << Src.rdbuf();

	// Pad to even length, for safety.
	if ((in.tellp() & 1) != 0) {
		in.put(0);
	}

	in.seekg(0);
	size_t const PadMask = ModulePadding - 1;

	while (true) {
		Format::decode(in, Dst);
		if (Dst.tellp() >= FullSize) {
			break;
		}

		// Skip padding between modules
		size_t paddingEnd = (size_t(in.tellg()) + PadMask) & ~PadMask;
		in.seekg(paddingEnd);
	}

	return true;
}

template <typename Format, size_t DefaultModuleSize, size_t DefaultModulePadding>
bool ModuledAdaptor<Format, DefaultModuleSize, DefaultModulePadding>::moduled_encode(
	std::istream &Src,
	std::ostream &Dst,
	size_t ModuleSize,
	size_t const ModulePadding
) {
	Src.seekg(0, std::ios::end);
	size_t FullSize = Src.tellg();
	Src.seekg(0);
	std::vector<unsigned char> data;
	data.resize(FullSize);
	std::vector<unsigned char>::const_iterator ptr = data.cbegin();
	Src.read(reinterpret_cast<char*>(&(data.front())), data.size());

	size_t const PadMask = ModulePadding - 1;

	BigEndian::Write2(Dst, FullSize);
	std::stringstream sout(std::ios::in | std::ios::out | std::ios::binary);

	while (FullSize > ModuleSize) {
		// We want to manage internal padding for all modules but the last.
		PadMaskBits = 8 * ModulePadding - 1u;
		Format::encode(sout, &(*ptr), ModuleSize);
		FullSize -= ModuleSize;
		ptr += ModuleSize;

		// Padding between modules
		long long paddingEnd = (size_t(sout.tellp()) + PadMask) & ~PadMask;
		for (; sout.tellp() < paddingEnd; sout.put(0)) {
		}
	}

	PadMaskBits = 7u;
	Format::encode(sout, &(*ptr), FullSize);

	// Pad to even size.
	Dst << sout.rdbuf();
	if ((Dst.tellp() & 1) != 0) {
		Dst.put(0);
	}
	return true;
}

#endif // __LIB_MODULED_ADAPTOR_H
