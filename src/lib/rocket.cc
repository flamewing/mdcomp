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
			// Dictionary match: 1-bit descriptor, 10-bit distance, 6-bit length.
			ignore_unused_variable_warning(dist, len);
			return 1 + 10 + 6;
		}
		// Given an edge, computes how many bits are used in the descriptor field.
		constexpr static size_t desc_bits(AdjListNode const &edge) noexcept {
			// Rocket always uses a single bit descriptor.
			ignore_unused_variable_warning(edge);
			return 1;
		}
		// Rocket finds no additional matches over normal LZSS.
		// TODO: Lies. Plane maps rely on the buffer initially containing 0x20's
		constexpr static void extra_matches(stream_t const *data,
			                      size_t basenode,
			                      size_t ubound, size_t lbound,
			                      LZSSGraph<RocketAdaptor>::MatchVector &matches) noexcept {
			ignore_unused_variable_warning(data, basenode, ubound, lbound, matches);
		}
		// Rocket needs no additional padding at the end-of-file.
		constexpr static size_t get_padding(size_t totallen) noexcept {
			ignore_unused_variable_warning(totallen);
			return 0;
		}
	};

	using RockGraph = LZSSGraph<RocketAdaptor>;
	using RockOStream = LZSSOStream<RocketAdaptor>;
	using RockIStream = LZSSIStream<RocketAdaptor>;

public:
	static void decode(istream &in, iostream &Dst, uint16_t Size) {
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
				unsigned char Byte = Read1(in);
				Write1(Dst, Byte);
				buffer[buffer_index++] = Byte;
				buffer_index &= 0x3FF;
			} else {
				// Dictionary match.
				// Distance and length of match.
				size_t high = src.getbyte(),
					   low = src.getbyte();

				size_t length = (high&0xFC)>>2,
					   index = ((high&3)<<8)|low;

				for (size_t i = 0; i <= length; i++) {
					unsigned char Byte = buffer[index++];
					index &= 0x3FF;
					Write1(Dst, Byte);
					buffer[buffer_index++] = Byte;
					buffer_index &= 0x3FF;
				}
			}
		}
	}

	static void encode(ostream &Dst, unsigned char const *&Data, size_t const Size) {
		// Compute optimal Rocket parsing of input file.
		RockGraph enc(Data, Size);
		RockGraph::AdjList list = enc.find_optimal_parse();
		RockOStream out(Dst);

		size_t pos = 0;
		// Go through each edge in the optimal path.
		for (RockGraph::AdjList::const_iterator it = list.begin();
			    it != list.end(); ++it) {
			AdjListNode const &edge = *it;
			size_t len = edge.get_length(), dist = edge.get_distance();
			// The weight of each edge uniquely identifies how it should be written.
			// NOTE: This needs to be changed for other LZSS schemes.
			if (len == 1) {
				// Symbolwise match.
				out.descbit(1);
				out.putbyte(Data[pos]);
			} else {
				// Dictionary match.
				out.descbit(0);
				uint16_t index = (0x3C0 + pos - dist) & 0x3FF;
				out.putbyte(((len-1)<<2)|(index>>8));
				out.putbyte(index);
			}
			// Go to next position.
			pos = edge.get_dest();
		}
	}
};

bool rocket::decode(istream &Src, iostream &Dst) {
	Src.ignore(2);
	size_t Size = BigEndian::Read2(Src);

	size_t Location = Src.tellg();
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
