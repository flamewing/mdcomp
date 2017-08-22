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

#include <cstdint>
#include <istream>
#include <ostream>
#include <sstream>

#include "kosinski.h"
#include "bigendian_io.h"
#include "bitstream.h"
#include "ignore_unused_variable_warning.h"
#include "lzss.h"

using namespace std;

template<>
size_t moduled_kosinski::PadMaskBits = 1u;

class kosinski_internal {
	// NOTE: This has to be changed for other LZSS-based compression schemes.
	struct KosinskiAdaptor {
		using stream_t = unsigned char;
		using descriptor_t = uint16_t;
		using descriptor_endian_t = littleendian<descriptor_t>;
		// Number of bits on descriptor bitfield.
		constexpr static size_t const NumDescBits = sizeof(descriptor_t) * 8;
		// Number of bits used in descriptor bitfield to signal the end-of-file
		// marker sequence.
		constexpr static size_t const NumTermBits = 2;
		// Flag that tells the compressor that new descriptor fields are needed
		// as soon as the last bit in the previous one is used up.
		constexpr static bool const NeedEarlyDescriptor = true;
		// Flag that marks the descriptor bits as being in little-endian bit
		// order (that is, lowest bits come out first).
		constexpr static bool const DescriptorLittleEndianBits = true;
		// Size of the search buffer.
		constexpr static size_t const SearchBufSize = 8192;
		// Size of the look-ahead buffer.
		constexpr static size_t const LookAheadBufSize = 256;
		// Total size of the sliding window.
		constexpr static size_t const SlidingWindowSize = SearchBufSize + LookAheadBufSize;
		// Computes the cost of a symbolwise encoding, that is, the cost of encoding
		// one single symbol..
		constexpr static size_t symbolwise_weight() noexcept {
			// Symbolwise match: 1-bit descriptor, 8-bit length.
			return 1 + 8;
		}
		// Computes the cost of covering all of the "len" vertices starting from
		// "off" vertices ago, for matches with len > 1.
		// A return of "numeric_limits<size_t>::max()" means "infinite",
		// or "no edge".
		constexpr static size_t dictionary_weight(size_t dist, size_t len) noexcept {
			// Preconditions:
			// len > 1 && len <= LookAheadBufSize && dist != 0 && dist <= SearchBufSize
			if (len == 2 && dist > 256) {
				// Can't represent this except by inlining both nodes.
				return numeric_limits<size_t>::max();   // "infinite"
			} else if (len <= 5 && dist <= 256) {
				// Inline dictionary match: 2-bit descriptor, 2-bit count, 8-bit distance.
				return 2 + 2 + 8;
			} else if (len >= 3 && len <= 9) {
				// Separate dictionary match, short form: 2-bit descriptor, 13-bit distance,
				// 3-bit length.
				return 2 + 13 + 3;
			} else { //if (len >= 3 && len <= 256)
				// Separate dictionary match, long form: 2-bit descriptor, 13-bit distance,
				// 3-bit marker (zero), 8-bit length.
				return 2 + 13 + 8 + 3;
			}
		}
		// Given an edge, computes how many bits are used in the descriptor field.
		static size_t desc_bits(AdjListNode const &edge) noexcept {
			// Since Kosinski non-descriptor data is always 1, 2 or 3 bytes in length,
			// this is a quick way to compute it.
			return edge.get_weight() & 7;
		}
		// Kosinski finds no additional matches over normal LZSS.
		constexpr static void extra_matches(stream_t const *data,
		                                    size_t basenode,
		                                    size_t ubound, size_t lbound,
		                                    LZSSGraph<KosinskiAdaptor>::MatchVector &matches) noexcept {
			ignore_unused_variable_warning(data, basenode, ubound, lbound, matches);
		}
		// KosinskiM needs to pad each module to a multiple of 16 bytes.
		static size_t get_padding(size_t totallen) noexcept {
			// Add in the size of the end-of-file marker.
			size_t padding = totallen + 3 * 8;
			return ((padding + moduled_kosinski::PadMaskBits) & ~moduled_kosinski::PadMaskBits) - totallen;
		}
	};

	using KosIStream = LZSSIStream<KosinskiAdaptor>;
	using KosGraph = LZSSGraph<KosinskiAdaptor>;
	using KosOStream = LZSSOStream<KosinskiAdaptor>;

public:
	static void decode(istream &in, iostream &Dst) {
		KosIStream src(in);

		while (in.good()) {
			if (src.descbit() != 0u) {
				// Symbolwise match.
				Write1(Dst, src.getbyte());
			} else {
				// Dictionary matches.
				// Count and distance
				size_t Count = 0;
				size_t distance = 0;

				if (src.descbit() != 0u) {
					// Separate dictionary match.
					unsigned char Low = src.getbyte(), High = src.getbyte();

					Count = size_t(High & 0x07);

					if (Count == 0u) {
						// 3-byte dictionary match.
						Count = src.getbyte();
						if (Count == 0u) {
							break;
						} else if (Count == 1) {
							continue;
						}
						Count += 1;
					} else {
						// 2-byte dictionary match.
						Count += 2;
					}

					distance = (~size_t(0x1FFF)) | (size_t(0xF8 & High) << 5) | size_t(Low);
				} else {
					// Inline dictionary match.
					unsigned char Low  = src.descbit(),
						          High = src.descbit();

					Count = ((size_t(Low) << 1) | size_t(High)) + 2;

					distance = src.getbyte();
					distance |= ~size_t(0xFF);
				}

				for (size_t i = 0; i < Count; i++) {
					size_t Pointer = Dst.tellp();
					Dst.seekg(Pointer + distance);
					unsigned char Byte = Read1(Dst);
					Dst.seekp(Pointer);
					Write1(Dst, Byte);
				}
			}
		}
	}

	static void encode(ostream &Dst, unsigned char const *Data, size_t const Size) {
		// Compute optimal Kosinski parsing of input file.
		KosGraph enc(Data, Size);
		typename KosGraph::AdjList list = enc.find_optimal_parse();
		KosOStream out(Dst);

		size_t pos = 0;
		// Go through each edge in the optimal path.
		for (typename KosGraph::AdjList::const_iterator it = list.begin();
			    it != list.end(); ++it) {
			AdjListNode const &edge = *it;
			size_t len = edge.get_length(), dist = edge.get_distance();
			// The weight of each edge uniquely identifies how it should be written.
			// NOTE: This needs to be changed for other LZSS schemes.
			switch (edge.get_weight()) {
				case 9:
					// Symbolwise match.
					out.descbit(1);
					out.putbyte(Data[pos]);
					break;
				case 12:
					// Inline dictionary match.
					out.descbit(0);
					out.descbit(0);
					len -= 2;
					out.descbit((len >> 1) & 1);
					out.descbit(len & 1);
					out.putbyte(-dist);
					break;
				case 18:
				case 26: {
					// Separate dictionary match.
					out.descbit(0);
					out.descbit(1);
					dist = (-dist) & 0x1FFF;
					uint16_t high = (dist >> 5) & 0xF8,
					         low  = (dist & 0xFF);
					if (edge.get_weight() == 18) {
						// 2-byte dictionary match.
						out.putbyte(low);
						out.putbyte(high | (len - 2));
					} else {
						// 3-byte dictionary match.
						out.putbyte(low);
						out.putbyte(high);
						out.putbyte(len - 1);
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

bool kosinski::decode(istream &Src, iostream &Dst) {
	size_t Location = Src.tellg();
	stringstream in(ios::in | ios::out | ios::binary);
	extract(Src, in);

	kosinski_internal::decode(in, Dst);

	Src.seekg(Location + in.tellg());
	return true;
}

bool kosinski::encode(ostream &Dst, unsigned char const *data, size_t const Size) {
	kosinski_internal::encode(Dst, data, Size);
	// Pad to even size.
	if ((Dst.tellp() & 1) != 0) {
		Dst.put(0);
	}
	return true;
}
