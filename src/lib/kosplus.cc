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
#include <sstream>

#include "kosplus.h"
#include "bigendian_io.h"
#include "bitstream.h"
#include "lzss.h"
#include "ignore_unused_variable_warning.h"

using namespace std;

// NOTE: This has to be changed for other LZSS-based compression schemes.
struct KosPlusAdaptor {
	typedef unsigned char stream_t;
	typedef unsigned char descriptor_t;
	typedef littleendian<descriptor_t> descriptor_endian_t;
	// Number of bits on descriptor bitfield.
	constexpr static size_t const NumDescBits = sizeof(descriptor_t) * 8;
	// Number of bits used in descriptor bitfield to signal the end-of-file
	// marker sequence.
	constexpr static size_t const NumTermBits = 2;
	// Flag that tells the compressor that new descriptor fields are needed
	// as soon as the last bit in the previous one is used up.
	constexpr static size_t const NeedEarlyDescriptor = 0;
	// Flag that marks the descriptor bits as being in little-endian bit
	// order (that is, lowest bits come out first).
	constexpr static size_t const DescriptorLittleEndianBits = 0;
	// Size of the search buffer.
	constexpr static size_t const SearchBufSize = 8192;
	// Size of the look-ahead buffer.
	constexpr static size_t const LookAheadBufSize = 264;
	// Total size of the sliding window.
	constexpr static size_t const SlidingWindowSize = SearchBufSize + LookAheadBufSize;
	// Computes the cost of a symbolwise encoding, that is, the cost of encoding
	// one single symbol..
	// Computes the cost of a symbolwise encoding, that is, the cost of encoding
	// one single symbol..
	constexpr static size_t symbolwise_weight() noexcept {
		// Literal: 1-bit descriptor, 8-bit length.
		return 1 + 8;
	}
	// Computes the cost of covering all of the "len" vertices starting from
	// "off" vertices ago, for matches with len > 1.
	// A return of "numeric_limits<size_t>::max()" means "infinite",
	// or "no edge".
	constexpr static size_t dictionary_weight(size_t dist, size_t len) noexcept {
		// Preconditions:
		// len > 1 && len <= szLookAhead && dist != 0 && dist <= szSearchBuffer
		if (len == 2 && dist > 256) {
			// Can't represent this except by inlining both nodes.
			return numeric_limits<size_t>::max();	// "infinite"
		} else if (len <= 5 && dist <= 256) {
			// Inline RLE: 2-bit descriptor, 2-bit count, 8-bit distance.
			return 2 + 2 + 8;
		} else if (len >= 3 && len <= 9) {
			// Separate RLE, short form: 2-bit descriptor, 13-bit distance,
			// 3-bit length.
			return 2 + 13 + 3;
		} else { //if (len >= 10 && len <= 264)
			// Separate RLE, long form: 2-bit descriptor, 13-bit distance,
			// 3-bit marker (zero), 8-bit length.
			return 2 + 13 + 8 + 3;
		}
	}
	// Given an edge, computes how many bits are used in the descriptor field.
	static size_t desc_bits(AdjListNode const &edge) noexcept {
		// Since KosPlus non-descriptor data is always 1, 2 or 3 bytes, this is
		// a quick way to compute it.
		return edge.get_weight() & 7;
	}
	// KosPlus finds no additional matches over normal LZSS.
	constexpr static void extra_matches(stream_t const *data,
	                          size_t basenode,
	                          size_t ubound, size_t lbound,
	                          LZSSGraph<KosPlusAdaptor>::MatchVector &matches) noexcept {
		ignore_unused_variable_warning(data, basenode, ubound, lbound, matches);
	}
	// KosPlusM needs no additional padding at the end-of-file.
	constexpr static size_t get_padding(size_t totallen, size_t padmask) noexcept {
		ignore_unused_variable_warning(totallen, padmask);
		return 0;
	}
};

class kosplus_internal {
public:
	static void decode(std::istream &in, std::iostream &Dst, size_t &DecBytes) {
		typedef LZSSIStream<KosPlusAdaptor> KosIStream;

		KosIStream src(in);

		while (in.good()) {
			if (src.descbit()) {
				Write1(Dst, src.getbyte());
				++DecBytes;
			} else {
				// Count and distance
				size_t Count = 0;
				streamoff distance = 0;

				if (src.descbit()) {
					unsigned char Low = src.getbyte(), High = src.getbyte();

					Count = size_t(High & 0x07);

					if (!Count) {
						Count = src.getbyte();
						if (!Count) {
							break;
						}
						Count += 9;
					} else {
						Count += 2;
					}

					distance = (~size_t(0x1FFF)) | (size_t(0xF8 & High) << 5) | size_t(Low);
				} else {
					unsigned char Low  = src.descbit(),
						          High = src.descbit();

					Count = ((size_t(Low) << 1) | size_t(High)) + 2;

					distance = src.getbyte();
					distance |= ~size_t(0xFF);
				}

				for (size_t i = 0; i < Count; i++) {
					streampos Pointer = Dst.tellp();
					Dst.seekg(Pointer + distance);
					unsigned char Byte = Read1(Dst);
					Dst.seekp(Pointer);
					Write1(Dst, Byte);
				}
				DecBytes += Count;
			}
		}
	}

	static void encode(std::ostream &Dst, unsigned char const *&Buffer,
	                   std::streamsize const BSize) {
		typedef LZSSGraph<KosPlusAdaptor> KosGraph;
		typedef LZSSOStream<KosPlusAdaptor> KosOStream;

		// Compute optimal KosPlus parsing of input file.
		KosGraph enc(Buffer, BSize, 1u);
		typename KosGraph::AdjList list = enc.find_optimal_parse();
		KosOStream out(Dst);

		streamoff pos = 0;
		// Go through each edge in the optimal path.
		for (typename KosGraph::AdjList::const_iterator it = list.begin();
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
						out.putbyte(len - 9);
					}
					break;
				}
				default:
					// This should be unreachable.
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
};

bool kosplus::decode(istream &Src, iostream &Dst,
                     streampos Location, bool Moduled) {
	size_t DecBytes = 0;

	Src.seekg(0, ios::end);
	streamsize sz = streamsize(Src.tellg()) - Location;
	Src.seekg(Location);

	stringstream in(ios::in | ios::out | ios::binary);
	in << Src.rdbuf();

	// Pad to even length, for safety.
	if ((sz & 1) != 0) {
		in.put(0x00);
	}

	in.seekg(0);

	if (Moduled) {
		size_t FullSize = BigEndian::Read2(in);
		
		while (true) {
			kosplus_internal::decode(in, Dst, DecBytes);
			if (DecBytes >= FullSize) {
				break;
			}
		}
	} else {
		kosplus_internal::decode(in, Dst, DecBytes);
	}

	return true;
}

bool kosplus::encode(istream &Src, ostream &Dst, bool Moduled, streamoff ModuleSize) {
	Src.seekg(0, ios::end);
	streamsize BSize = Src.tellg();
	Src.seekg(0);
	auto const Buffer = new char[BSize];
	unsigned char const *ptr = reinterpret_cast<unsigned char *>(Buffer);
	Src.read(Buffer, BSize);

	if (Moduled) {
		if (BSize > 65535) {  // Decompressed size would fill RAM or VRAM.
			return false;
		}

		streamoff FullSize = BSize, CompBytes = 0;

		if (BSize > ModuleSize) {
			BSize = ModuleSize;
		}

		BigEndian::Write2(Dst, FullSize);

		while (true) {
			kosplus_internal::encode(Dst, ptr, BSize);

			CompBytes += BSize;
			ptr += BSize;

			if (CompBytes >= FullSize) {
				break;
			}

			BSize = min(ModuleSize, FullSize - CompBytes);
		}
	} else {
		kosplus_internal::encode(Dst, ptr, BSize);
	}

	// Pad to even size.
	if ((Dst.tellp() & 1) != 0) {
		Dst.put(0);
	}

	delete [] Buffer;
	return true;
}