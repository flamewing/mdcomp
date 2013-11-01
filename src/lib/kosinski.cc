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

#include "kosinski.h"
#include "bigendian_io.h"
#include "bitstream.h"
#include "lzss.h"

void kosinski::decode_internal(std::istream &in, std::iostream &Dst, size_t &DecBytes) {
	ibitstream<unsigned short, littleendian<unsigned short> > bits(in);

	while (in.good()) {
		if (bits.pop()) {
			Write1(Dst, Read1(in));
			++DecBytes;
		} else {
			// Count and distance
			size_t Count = 0;
			std::streamoff distance = 0;

			if (bits.pop()) {
				unsigned char Low = Read1(in), High = Read1(in);

				Count = (size_t)(High & 0x07);

				if (!Count) {
					Count = Read1(in);
					if (!Count)
						break;
					else if (Count == 1)
						continue;
				} else {
					Count += 1;
				}

				distance = (~((std::streamoff)0x1FFF)) | ((std::streamoff)(0xF8 & High) << 5) | (std::streamoff)Low;
			} else {
				unsigned char Low  = bits.pop(),
				              High = bits.pop();

				Count = ((((size_t)Low) << 1) | ((size_t)High)) + 1;

				distance = Read1(in);
				distance |= (~((std::streamoff)0xFF));
			}

			for (size_t i = 0; i <= Count; i++) {
				std::streampos Pointer = Dst.tellp();
				Dst.seekg(Pointer + distance);
				unsigned char Byte = Read1(Dst);
				Dst.seekp(Pointer);
				Write1(Dst, Byte);
			}
			DecBytes += (Count + 1);
		}
	}
}

bool kosinski::decode(std::istream &Src, std::iostream &Dst,
                      std::streampos Location, bool Moduled) {
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

	if (Moduled) {
		size_t FullSize = BigEndian::Read2(in);
		while (true) {
			decode_internal(in, Dst, DecBytes);
			if (DecBytes >= FullSize)
				break;

			// Skip padding between modules
			size_t paddingEnd = (((size_t(in.tellg()) - 2) + 0xf) & ~0xf) + 2;
			in.seekg(paddingEnd);
		}
	} else
		decode_internal(in, Dst, DecBytes);

	return true;
}

// NOTE: This has to be changed for other LZSS-based compression schemes.
struct KosinskiAdaptor {
	typedef unsigned char  stream_t;
	typedef unsigned short descriptor_t;
	typedef littleendian<descriptor_t> descriptor_endian_t;
	enum {
		// Number of bits on descriptor bitfield.
		NumDescBits = sizeof(descriptor_t) * 8,
		// Number of bits used in descriptor bitfield to signal the end-of-file
		// marker sequence.
		NumTermBits = 2,
		// Flag that tells the compressor that new descriptor fields are needed
		// as soon as the last bit in the previous one is used up.
		NeedEarlyDescriptor = 1,
		// Flag that marks the descriptor bits as being in little-endian bit
		// order (that is, lowest bits come out first).
		DescriptorLittleEndianBits = 1
	};
	// Computes the cost of covering all of the "len" vertices starting from
	// "off" vertices ago.
	// A return of "std::numeric_limits<size_t>::max()" means "infinite",
	// or "no edge".
	static size_t calc_weight(size_t dist, size_t len) {
		// Preconditions:
		// len != 0 && len <= RecLen && dist != 0 && dist <= SlideWin
		if (len == 1)
			// Literal: 1-bit descriptor, 8-bit length.
			return 1 + 8;
		else if (len == 2 && dist > 256)
			// Can't represent this except by inlining both nodes.
			return std::numeric_limits<size_t>::max();	// "infinite"
		else if (len <= 5 && dist <= 256)
			// Inline RLE: 2-bit descriptor, 2-bit count, 8-bit distance.
			return 2 + 2 + 8;
		else if (len >= 3 && len <= 9)
			// Separate RLE, short form: 2-bit descriptor, 13-bit distance,
			// 3-bit length.
			return 2 + 13 + 3;
		else //if (len >= 3 && len <= 256)
			// Separate RLE, long form: 2-bit descriptor, 13-bit distance,
			// 3-bit marker (zero), 8-bit length.
			return 2 + 13 + 8 + 3;
	}
	// Given an edge, computes how many bits are used in the descriptor field.
	static size_t desc_bits(AdjListNode const &edge) {
		// Since Kosinski non-descriptor data is always 1, 2 or 3 bytes, this is
		// a quick way to compute it.
		return edge.get_weight() & 7;
	}
};

typedef LZSSGraph<KosinskiAdaptor> KosGraph;
typedef LZSSOStream<KosinskiAdaptor> KosOStream;

void kosinski::encode_internal(std::ostream &Dst, unsigned char const *&Buffer,
                               std::streamoff SlideWin, std::streamoff RecLen,
                               std::streamsize const BSize) {
	// Compute optimal Kosinski parsing of input file.
	KosGraph enc(Buffer, BSize, SlideWin, RecLen);
	KosGraph::AdjList list = enc.find_optimal_parse();
	KosOStream out(Dst);

	std::streamoff pos = 0;
	// Go through each edge in the optimal path.
	for (KosGraph::AdjList::const_iterator it = list.begin();
	        it != list.end(); ++it) {
		AdjListNode const &edge = *it;
		size_t len = edge.get_length(), dist = edge.get_distance();
		// The weight of each edge uniquely identifies how it should be written.
		// NOTE: This needs to be changed for other LZSS schemes.
		switch (edge.get_weight()) {
			case 9:
				// Literal.
				out.descbit(1);
				out.putbyte(Buffer[pos]);
				break;
			case 12:
				// Inline RLE.
				out.descbit(0);
				out.descbit(0);
				len -= 2;
				out.descbit((len >> 1) & 1);
				out.descbit(len & 1);
				out.putbyte(-dist);
				break;
			case 18:
			case 26: {
				// Separate RLE.
				out.descbit(0);
				out.descbit(1);
				dist = (-dist) & 0x1FFF;
				unsigned short high = (dist >> 5) & 0xF8,
				               low  = (dist & 0xFF);
				if (edge.get_weight() == 18) {
					// 2-byte RLE.
					out.putbyte(low);
					out.putbyte(high | (len - 2));
				} else {
					// 3-byte RLE.
					out.putbyte(low);
					out.putbyte(high);
					out.putbyte(len - 1);
				}
				break;
			}
			default:
				// This should be unreachable.
				//std::cerr << "Divide by cucumber error: impossible token length!" << std::endl;
				break;
		}
		// Go to next position.
		pos = edge.get_dest();
	}

	// Push descriptor for end-of-file marker.
	out.descbit(0);
	out.descbit(1);

	// Write end-of-file marker. Maybe use 0x00 0xF8 0x00 instead?
	out.putbyte(0x00);
	out.putbyte(0xF0);
	out.putbyte(0x00);
}

bool kosinski::encode(std::istream &Src, std::ostream &Dst, std::streamoff SlideWin,
                      std::streamoff RecLen, bool Moduled, std::streamoff ModuleSize) {
	Src.seekg(0, std::ios::end);
	std::streamsize BSize = Src.tellg();
	Src.seekg(0);
	unsigned char *const Buffer = new unsigned char[BSize];
	unsigned char const *ptr = Buffer;
	Src.read((char *)ptr, BSize);

	if (Moduled) {
		if (BSize > 65535)  // Decompressed size would fill RAM or VRAM.
			return false;

		std::streamoff FullSize = BSize, CompBytes = 0;

		if (BSize > ModuleSize)
			BSize = ModuleSize;

		BigEndian::Write2(Dst, FullSize);

		while (true) {
			encode_internal(Dst, ptr, SlideWin, RecLen, BSize);

			CompBytes += BSize;
			ptr += BSize;

			if (CompBytes >= FullSize)
				break;

			// Padding between modules
			size_t paddingEnd = (((size_t(Dst.tellp()) - 2) + 0xf) & ~0xf) + 2;
			std::streampos n = paddingEnd - size_t(Dst.tellp());

			for (size_t ii = 0; ii < n; ii++) {
				Dst.put(0x00);
			}

			BSize = std::min(ModuleSize, FullSize - CompBytes);
		}
	} else
		encode_internal(Dst, ptr, SlideWin, RecLen, BSize);

	// Pad to even size.
	if ((Dst.tellp() & 1) != 0)
		Dst.put(0);

	delete [] Buffer;
	return true;
}
