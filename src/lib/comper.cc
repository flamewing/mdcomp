/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * Copyright (C) Flamewing 2011-2013 <flamewing.sonic@gmail.com>
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

#include <istream>
#include <ostream>
#include <string>
#include <sstream>
#include <algorithm>
#include <vector>
#include <list>
#include <map>
#include <limits>

#include "comper.h"
#include "bigendian_io.h"
#include "bitstream.h"
#include "lzss.h"

void comper::decode_internal(std::istream &in, std::iostream &Dst) {
	ibitstream<unsigned short, bigendian<unsigned short> > bits(in);

	while (in.good()) {
		if (!bits.get()) {
			BigEndian::Write2(Dst, BigEndian::Read2(in));
		} else {
			// Distance and length of match.
			size_t distance = (0x100 - Read1(in)) * 2, length = Read1(in);
			if (length == 0) {
				break;
			}

			for (size_t i = 0; i <= length; i++) {
				std::streampos Pointer = Dst.tellp();
				Dst.seekg(std::streamoff(Pointer) - distance);
				unsigned short Word = BigEndian::Read2(Dst);
				Dst.seekp(Pointer);
				BigEndian::Write2(Dst, Word);
			}
		}
	}
}

bool comper::decode(std::istream &Src, std::iostream &Dst,
                    std::streampos Location) {
	size_t DecBytes = 0;

	Src.seekg(0, std::ios::end);
	std::streamsize sz = std::streamsize(Src.tellg()) - Location;
	Src.seekg(Location);

	std::stringstream in(std::ios::in | std::ios::out | std::ios::binary);
	in << Src.rdbuf();

	// Pad to even length, for safety.
	if ((sz & 1) != 0)
		in.put(0x00);

	in.seekg(0);
	decode_internal(in, Dst);
	return true;
}

// NOTE: This has to be changed for other LZSS-based compression schemes.
struct ComperAdaptor {
	typedef unsigned short stream_t;
	typedef unsigned short descriptor_t;
	typedef bigendian<descriptor_t> descriptor_endian_t;
	enum {
		// Number of bits on descriptor bitfield.
		NumDescBits = sizeof(descriptor_t) * 8,
		// Number of bits used in descriptor bitfield to signal the end-of-file
		// marker sequence.
		NumTermBits = 1,
		// Flag that tells the compressor that new descriptor fields are needed
		// as soon as the last bit in the previous one is used up.
		NeedEarlyDescriptor = 0,
		// Flag that marks the descriptor bits as being in little-endian bit
		// order (that is, lowest bits come out first).
		DescriptorLittleEndianBits = 0
	};
	// Computes the cost of covering all of the "len" vertices starting from
	// "off" vertices ago.
	// A return of "std::numeric_limits<size_t>::max()" means "infinite",
	// or "no edge".
	static size_t calc_weight(size_t dist, size_t len) {
		// Preconditions:
		// len != 0 && len <= RecLen && dist != 0 && dist <= SlideWin
		if (len == 1)
			// Literal: 1-bit descriptor, 16-bit length.
			return 1 + 16;
		else
			// RLE: 1-bit descriptor, 8-bit distance, 8-bit length.
			return 1 + 8 + 8;
	}
	// Given an edge, computes how many bits are used in the descriptor field.
	static size_t desc_bits(AdjListNode const &edge) {
		// Comper always uses a single bit descriptor.
		return 1;
	}
};

typedef LZSSGraph<ComperAdaptor> CompGraph;
typedef LZSSOStream<ComperAdaptor> CompOStream;

void comper::encode_internal(std::ostream &Dst, unsigned char const *&Buffer,
                             std::streamsize const BSize) {
	// Compute optimal Comper parsing of input file.
	CompGraph enc(Buffer, BSize, 256, 256);
	CompGraph::AdjList list = enc.find_optimal_parse();
	CompOStream out(Dst);

	std::streamoff pos = 0;
	// Go through each edge in the optimal path.
	for (CompGraph::AdjList::const_iterator it = list.begin();
	        it != list.end(); ++it) {
		AdjListNode const &edge = *it;
		size_t len = edge.get_length(), dist = edge.get_distance();
		// The weight of each edge uniquely identifies how it should be written.
		// NOTE: This needs to be changed for other LZSS schemes.
		if (edge.get_length() == 1) {
			// Literal.
			out.descbit(0);
			out.putbyte(Buffer[pos]);
			out.putbyte(Buffer[pos + 1]);
		} else {
			// RLE.
			out.descbit(1);
			out.putbyte(-edge.get_distance());
			out.putbyte(edge.get_length() - 1);
		}
		// Go to next position.
		pos = edge.get_dest() * 2;
	}

	// Push descriptor for end-of-file marker.
	out.descbit(1);

	out.putbyte(0x00);
	out.putbyte(0x00);
}

bool comper::encode(std::istream &Src, std::ostream &Dst) {
	Src.seekg(0, std::ios::end);
	std::streamsize BSize = Src.tellg();
	Src.seekg(0);
	unsigned char *const Buffer = new unsigned char[BSize];
	unsigned char const *ptr = Buffer;
	Src.read((char *)ptr, BSize);

	encode_internal(Dst, ptr, BSize);

	delete [] Buffer;
	return true;
}
