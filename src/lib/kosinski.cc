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
#include <iostream>
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
		enum class EdgeType : size_t {
			invalid,
			symbolwise,
			dictionary_inline,
			dictionary_short,
			dictionary_long
		};
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
		// Computes the type of edge that covers all of the "len" vertices starting from
		// "off" vertices ago.
		// Returns EdgeType::invalid if there is no such edge.
		constexpr static EdgeType match_type(size_t const dist, size_t const len) noexcept {
			// Preconditions:
			// len >= 1 && len <= LookAheadBufSize && dist != 0 && dist <= SearchBufSize
			if (len == 1) {
				return EdgeType::symbolwise;
			} else if (len == 2 && dist > 256) {
				// Can't represent this except by inlining both nodes.
				return EdgeType::invalid;
			} else if (len <= 5 && dist <= 256) {
				return EdgeType::dictionary_inline;
			} else if (len >= 3 && len <= 9) {
				return EdgeType::dictionary_short;
			} else { //if (len >= 3 && len <= 256)
				return EdgeType::dictionary_long;
			}
		}
		// Given an edge type, computes how many bits are used in the descriptor field.
		constexpr static size_t desc_bits(EdgeType const type) noexcept {
			switch (type) {
				case EdgeType::symbolwise:
					// 1-bit describtor.
					return 1;
				case EdgeType::dictionary_inline:
					// 2-bit descriptor, 2-bit count.
					return 2 + 2;
				case EdgeType::dictionary_short:
				case EdgeType::dictionary_long:
					// 2-bit descriptor.
					return 2;
				default:
					return numeric_limits<size_t>::max();
			}
		}
		// Given an edge type, computes how many bits are used in total by this edge.
		// A return of "numeric_limits<size_t>::max()" means "infinite",
		// or "no edge".
		constexpr static size_t edge_weight(EdgeType const type) noexcept {
			switch (type) {
				case EdgeType::symbolwise:
					// 8-bit value.
					return desc_bits(type) + 8;
				case EdgeType::dictionary_inline:
					// 8-bit distance.
					return desc_bits(type) + 8;
				case EdgeType::dictionary_short:
					// 13-bit distance, 3-bit length.
					return desc_bits(type) + 13 + 3;
				case EdgeType::dictionary_long:
					// 13-bit distance, 3-bit marker (zero),
					// 8-bit length.
					return desc_bits(type) + 13 + 8 + 3;
				default:
					return numeric_limits<size_t>::max();
			}
		}
		// Kosinski finds no additional matches over normal LZSS.
		constexpr static void extra_matches(stream_t const *data,
		                                    size_t const basenode,
		                                    size_t const ubound, size_t const lbound,
		                                    LZSSGraph<KosinskiAdaptor>::MatchVector &matches) noexcept {
			ignore_unused_variable_warning(data, basenode, ubound, lbound, matches);
		}
		// KosinskiM needs to pad each module to a multiple of 16 bytes.
		static size_t get_padding(size_t const totallen) noexcept {
			// Add in the size of the end-of-file marker.
			size_t padding = totallen + 3 * 8;
			return ((padding + moduled_kosinski::PadMaskBits) & ~moduled_kosinski::PadMaskBits) - totallen;
		}
	};

public:
	static void decode(istream &in, iostream &Dst) {
		using KosIStream = LZSSIStream<KosinskiAdaptor>;

		KosIStream src(in);

		while (in.good()) {
			if (src.descbit() != 0u) {
				// Symbolwise match.
				Write1(Dst, src.getbyte());
			} else {
				// Dictionary matches.
				// Count and distance
				size_t Count = 0u;
				size_t distance = 0u;

				if (src.descbit() != 0u) {
					// Separate dictionary match.
					size_t Low = src.getbyte(), High = src.getbyte();

					Count = High & 0x07u;

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

					distance = 0x2000u - (((0xF8u & High) << 5) | Low);
				} else {
					// Inline dictionary match.
					size_t High = src.descbit(), Low  = src.descbit();

					Count = ((High << 1) | Low) + 2;

					distance = 0x100u - src.getbyte();
				}

				for (size_t i = 0; i < Count; i++) {
					size_t Pointer = Dst.tellp();
					Dst.seekg(Pointer - distance);
					unsigned char Byte = Read1(Dst);
					Dst.seekp(Pointer);
					Write1(Dst, Byte);
				}
			}
		}
	}

	static void encode(ostream &Dst, unsigned char const *Data, size_t const Size) {
		using EdgeType = typename KosinskiAdaptor::EdgeType;
		using KosGraph = LZSSGraph<KosinskiAdaptor>;
		using KosOStream = LZSSOStream<KosinskiAdaptor>;

		// Compute optimal Kosinski parsing of input file.
		KosGraph enc(Data, Size);
		typename KosGraph::AdjList list = enc.find_optimal_parse();
		KosOStream out(Dst);

		size_t pos = 0;
		// Go through each edge in the optimal path.
		for (auto const &edge : list) {
			switch (edge.get_type()) {
				case EdgeType::symbolwise:
					out.descbit(1);
					out.putbyte(Data[pos]);
					break;
				case EdgeType::dictionary_inline: {
					out.descbit(0);
					out.descbit(0);
					size_t const len  = edge.get_length() - 2,
					             dist = 0x100u - edge.get_distance();
					out.descbit((len >> 1) & 1);
					out.descbit(len & 1);
					out.putbyte(dist);
					break;
				}
				case EdgeType::dictionary_short:
				case EdgeType::dictionary_long: {
					out.descbit(0);
					out.descbit(1);
					size_t const len  = edge.get_length(),
					             dist = 0x2000u - edge.get_distance();
					uint16_t high = (dist >> 5) & 0xF8u,
					         low  = (dist & 0xFFu);
					if (edge.get_type() == EdgeType::dictionary_short) {
						out.putbyte(low);
						out.putbyte(high | (len - 2));
					} else {
						out.putbyte(low);
						out.putbyte(high);
						out.putbyte(len - 1);
					}
					break;
				}
				default:
					// This should be unreachable.
					std::cerr << "Compression produced invalid edge type " << static_cast<size_t>(edge.get_type()) << std::endl;
					__builtin_unreachable();
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
	size_t const Location = Src.tellg();
	stringstream in(ios::in | ios::out | ios::binary);
	extract(Src, in);

	kosinski_internal::decode(in, Dst);

	Src.seekg(Location + in.tellg());
	return true;
}

bool kosinski::encode(ostream &Dst, unsigned char const *data, size_t const Size) {
	kosinski_internal::encode(Dst, data, Size);
	return true;
}
