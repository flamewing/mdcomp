/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * Copyright (C) Flamewing 2013-2016 <flamewing.sonic@gmail.com>
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
#include <istream>
#include <ostream>
#include <sstream>

#include "comper.h"
#include "bigendian_io.h"
#include "bitstream.h"
#include "ignore_unused_variable_warning.h"
#include "lzss.h"

using namespace std;

template<>
size_t moduled_comper::PadMaskBits = 1u;

class comper_internal {
	// NOTE: This has to be changed for other LZSS-based compression schemes.
	struct ComperAdaptor {
		using stream_t = uint16_t;
		using descriptor_t = uint16_t;
		using descriptor_endian_t = bigendian<descriptor_t>;
		// Number of bits on descriptor bitfield.
		constexpr static size_t const NumDescBits = sizeof(descriptor_t) * 8;
		// Number of bits used in descriptor bitfield to signal the end-of-file
		// marker sequence.
		constexpr static size_t const NumTermBits = 1;
		// Flag that tells the compressor that new descriptor fields is needed
		// when a new bit is needed and all bits in the previous one have been
		// used up.
		constexpr static bool const NeedEarlyDescriptor = false;
		// Flag that marks the descriptor bits as being in big-endian bit
		// order (that is, highest bits come out first).
		constexpr static bool const DescriptorLittleEndianBits = false;
		// Size of the search buffer.
		constexpr static size_t const SearchBufSize = 256;
		// Size of the look-ahead buffer.
		constexpr static size_t const LookAheadBufSize = 256;
		// Total size of the sliding window.
		constexpr static size_t const SlidingWindowSize = SearchBufSize + LookAheadBufSize;
		// Computes the cost of a symbolwise encoding, that is, the cost of encoding
		// one single symbol..
		constexpr static size_t symbolwise_weight() noexcept {
			// Symbolwise match: 1-bit descriptor, 16-bit length.
			return 1 + 16;
		}
		// Computes the cost of covering all of the "len" vertices starting from
		// "off" vertices ago, for matches with len > 1.
		// A return of "numeric_limits<size_t>::max()" means "infinite",
		// or "no edge".
		constexpr static size_t dictionary_weight(size_t dist, size_t len) noexcept {
			// Preconditions:
			// len > 1 && len <= LookAheadBufSize && dist != 0 && dist <= SearchBufSize
			// Dictionary match: 1-bit descriptor, 8-bit distance, 8-bit length.
			ignore_unused_variable_warning(dist, len);
			return 1 + 8 + 8;
		}
		// Given an edge, computes how many bits are used in the descriptor field.
		constexpr static size_t desc_bits(AdjListNode const &edge) noexcept {
			// Comper always uses a single bit descriptor.
			ignore_unused_variable_warning(edge);
			return 1;
		}
		// Comper finds no additional matches over normal LZSS.
		constexpr static void extra_matches(stream_t const *data,
		                                    size_t basenode,
		                                    size_t ubound, size_t lbound,
		                                    LZSSGraph<ComperAdaptor>::MatchVector &matches) noexcept {
			ignore_unused_variable_warning(data, basenode, ubound, lbound, matches);
		}
		// Comper needs no additional padding at the end-of-file.
		constexpr static size_t get_padding(size_t totallen) noexcept {
			ignore_unused_variable_warning(totallen);
			return 0;
		}
	};

	using CompGraph = LZSSGraph<ComperAdaptor>;
	using CompOStream = LZSSOStream<ComperAdaptor>;
	using CompIStream = LZSSIStream<ComperAdaptor>;

public:
	static void decode(istream &in, iostream &Dst) {
		CompIStream src(in);

		while (in.good()) {
			if (src.descbit() == 0u) {
				// Symbolwise match.
				BigEndian::Write2(Dst, BigEndian::Read2(in));
			} else {
				// Dictionary match.
				// Distance and length of match.
				size_t distance = (0x100 - src.getbyte()) * 2,
					   length = src.getbyte();
				if (length == 0) {
					break;
				}

				for (size_t i = 0; i <= length; i++) {
					size_t Pointer = Dst.tellp();
					Dst.seekg(Pointer - distance);
					uint16_t Word = BigEndian::Read2(Dst);
					Dst.seekp(Pointer);
					BigEndian::Write2(Dst, Word);
				}
			}
		}
	}

	static void encode(ostream &Dst, unsigned char const *Data, size_t const Size) {
		// Compute optimal Comper parsing of input file.
		CompGraph enc(Data, Size);
		CompGraph::AdjList list = enc.find_optimal_parse();
		CompOStream out(Dst);

		size_t pos = 0;
		// Go through each edge in the optimal path.
		for (CompGraph::AdjList::const_iterator it = list.begin();
			    it != list.end(); ++it) {
			AdjListNode const &edge = *it;
			size_t len = edge.get_length(), dist = edge.get_distance();
			// The weight of each edge uniquely identifies how it should be written.
			// NOTE: This needs to be changed for other LZSS schemes.
			if (len == 1) {
				// Symbolwise match.
				out.descbit(0);
				out.putbyte(Data[pos]);
				out.putbyte(Data[pos + 1]);
			} else {
				// Dictionary match.
				out.descbit(1);
				out.putbyte(-dist);
				out.putbyte(len - 1);
			}
			// Go to next position.
			pos = edge.get_dest() * 2;
		}

		// Push descriptor for end-of-file marker.
		out.descbit(1);

		out.putbyte(0);
		out.putbyte(0);
	}
};

bool comper::decode(istream &Src, iostream &Dst) {
	size_t Location = Src.tellg();
	stringstream in(ios::in | ios::out | ios::binary);
	extract(Src, in);

	comper_internal::decode(in, Dst);
	Src.seekg(Location + in.tellg());
	return true;
}

bool comper::encode(ostream &Dst, unsigned char const *data, size_t const Size) {
	comper_internal::encode(Dst, data, Size);
	return true;
}
