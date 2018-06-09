/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * Copyright (C) Clownacy 2016
 * Copyright (C) Flamewing 2016 <flamewing.sonic@gmail.com>
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

#include "rocket.h"
#include "bigendian_io.h"
#include "bitstream.h"
#include "ignore_unused_variable_warning.h"
#include "lzss.h"

using namespace std;

template<>
size_t moduled_rocket::PadMaskBits = 1u;

class rocket_internal {
	// NOTE: This has to be changed for other LZSS-based compression schemes.
	struct RocketAdaptor {
		using stream_t = unsigned char;
		using descriptor_t = unsigned char;
		using descriptor_endian_t = littleendian<descriptor_t>;
		enum class EdgeType : size_t {
			invalid,
			symbolwise,
			dictionary
		};
		// Number of bits on descriptor bitfield.
		constexpr static size_t const NumDescBits = sizeof(descriptor_t) * 8;
		// Number of bits used in descriptor bitfield to signal the end-of-file
		// marker sequence.
		constexpr static size_t const NumTermBits = 0;
		// Flag that tells the compressor that new descriptor fields is needed
		// when a new bit is needed and all bits in the previous one have been
		// used up.
		constexpr static bool const NeedEarlyDescriptor = false;
		// Flag that marks the descriptor bits as being in big-endian bit
		// order (that is, highest bits come out first).
		constexpr static bool const DescriptorLittleEndianBits = true;
		// Size of the search buffer.
		constexpr static size_t const SearchBufSize = 0x400;
		// Size of the look-ahead buffer.
		constexpr static size_t const LookAheadBufSize = 0x40;
		// Total size of the sliding window.
		constexpr static size_t const SlidingWindowSize = SearchBufSize + LookAheadBufSize;
		// Computes the cost of a symbolwise encoding, that is, the cost of encoding
		// one single symbol..
		// Computes the type of edge that covers all of the "len" vertices starting from
		// "off" vertices ago.
		// Returns EdgeType::invalid if there is no such edge.
		constexpr static EdgeType match_type(size_t const dist, size_t const len) noexcept {
			// Preconditions:
			// len >= 1 && len <= LookAheadBufSize && dist != 0 && dist <= SearchBufSize
			// Dictionary match: 1-bit descriptor, 8-bit distance, 8-bit length.
			ignore_unused_variable_warning(dist);
			if (len == 1) {
				return EdgeType::symbolwise;
			} else {
				return EdgeType::dictionary;
			}
		}
		// Given an edge type, computes how many bits are used in the descriptor field.
		constexpr static size_t desc_bits(EdgeType const type) noexcept {
			// Rocket always uses a single bit descriptor.
			ignore_unused_variable_warning(type);
			return 1;
		}
		// Given an edge type, computes how many bits are used in total by this edge.
		// A return of "numeric_limits<size_t>::max()" means "infinite",
		// or "no edge".
		constexpr static size_t edge_weight(EdgeType const type) noexcept {
			switch (type) {
				case EdgeType::symbolwise:
					// 8-bit value.
					return desc_bits(type) + 8;
				case EdgeType::dictionary:
					// 10-bit distance, 6-bit length.
					return desc_bits(type) + 10 + 6;
				default:
					return numeric_limits<size_t>::max();
			}
		}
		// Rocket finds no additional matches over normal LZSS.
		// TODO: Lies. Plane maps rely on the buffer initially containing 0x20's
		constexpr static void extra_matches(stream_t const *data,
			                      size_t const basenode,
			                      size_t const ubound, size_t const lbound,
			                      LZSSGraph<RocketAdaptor>::MatchVector &matches) noexcept {
			ignore_unused_variable_warning(data, basenode, ubound, lbound, matches);
		}
		// Rocket needs no additional padding at the end-of-file.
		constexpr static size_t get_padding(size_t const totallen) noexcept {
			ignore_unused_variable_warning(totallen);
			return 0;
		}
	};

public:
	static void decode(istream &in, iostream &Dst, uint16_t const Size) {
		using RockIStream = LZSSIStream<RocketAdaptor>;

		RockIStream src(in);

		// Initialise buffer (needed by Rocket Knight Adventures plane maps)
		// TODO: Make compressor perform matches for this
		unsigned char buffer[0x400];
		for (size_t i = 0; i < 0x3C0; i++) {
			buffer[i] = 0x20;
		}
		size_t buffer_index = 0x3C0;

		while (in.good() && in.tellg() < Size) {
			if (src.descbit() != 0u) {
				// Symbolwise match.
				unsigned char const Byte = Read1(in);
				Write1(Dst, Byte);
				buffer[buffer_index++] = Byte;
				buffer_index &= 0x3FF;
			} else {
				// Dictionary match.
				// Distance and length of match.
				size_t const high = src.getbyte(),
					         low = src.getbyte();

				size_t const length = (high&0xFC)>>2;
				size_t index = ((high&3)<<8)|low;

				for (size_t i = 0; i <= length; i++) {
					unsigned char const Byte = buffer[index++];
					index &= 0x3FF;
					Write1(Dst, Byte);
					buffer[buffer_index++] = Byte;
					buffer_index &= 0x3FF;
				}
			}
		}
	}

	static void encode(ostream &Dst, unsigned char const *&Data, size_t const Size) {
		using EdgeType = typename RocketAdaptor::EdgeType;
		using RockGraph = LZSSGraph<RocketAdaptor>;
		using RockOStream = LZSSOStream<RocketAdaptor>;

		// Compute optimal Rocket parsing of input file.
		RockGraph enc(Data, Size);
		RockGraph::AdjList list = enc.find_optimal_parse();
		RockOStream out(Dst);

		size_t pos = 0;
		// Go through each edge in the optimal path.
		for (auto const &edge : list) {
			switch (edge.get_type()) {
				case EdgeType::symbolwise:
					out.descbit(1);
					out.putbyte(Data[pos]);
					break;
				case EdgeType::dictionary: {
					size_t const len  = edge.get_length(),
					             dist = edge.get_distance();
					out.descbit(0);
					uint16_t const index = (0x3C0 + pos - dist) & 0x3FF;
					out.putbyte(((len-1)<<2)|(index>>8));
					out.putbyte(index);
					break;
				}
				default:
					// This should be unreachable.
					std::cerr << "Compression produced invalid edge type " << static_cast<size_t>(edge.get_type()) << std::endl;
					__builtin_unreachable();
			};
			// Go to next position.
			pos = edge.get_dest();
		}
	}
};

bool rocket::decode(istream &Src, iostream &Dst) {
	Src.ignore(2);
	size_t const Size = BigEndian::Read2(Src);

	size_t const Location = Src.tellg();
	stringstream in(ios::in | ios::out | ios::binary);
	extract(Src, in);

	rocket_internal::decode(in, Dst, Size);
	Src.seekg(Location + in.tellg());
	return true;
}

bool rocket::encode(ostream &Dst, unsigned char const *data, size_t const Size) {
	// Internal buffer.
	stringstream outbuff(ios::in | ios::out | ios::binary);
	rocket_internal::encode(outbuff, data, Size);

	// Fill in header
	BigEndian::Write2(Dst, Size);					// Size of decompressed file
	BigEndian::Write2(Dst, outbuff.tellp());		// Size of compressed file

	outbuff.seekg(0);
	Dst << outbuff.rdbuf();
	return true;
}
