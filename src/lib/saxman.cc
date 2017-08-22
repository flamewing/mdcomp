/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * Copyright (C) Flamewing 2013-2016 <flamewing.sonic@gmail.com>
 * Very loosely based on code by the KENS Project Development Team
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

#include "saxman.h"
#include "bigendian_io.h"
#include "bitstream.h"
#include "ignore_unused_variable_warning.h"
#include "lzss.h"

using namespace std;

template<>
size_t moduled_saxman::PadMaskBits = 1u;

class saxman_internal {
	// NOTE: This has to be changed for other LZSS-based compression schemes.
	struct SaxmanAdaptor {
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
		// Flag that marks the descriptor bits as being in little-endian bit
		// order (that is, lowest bits come out first).
		constexpr static bool const DescriptorLittleEndianBits = true;
		// Size of the search buffer.
		constexpr static size_t const SearchBufSize = 4096;
		// Size of the look-ahead buffer.
		constexpr static size_t const LookAheadBufSize = 18;
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
			ignore_unused_variable_warning(dist);
			if (len == 2) {
				return numeric_limits<size_t>::max();   // "infinite"
			} else {
				// Dictionary match: 1-bit descriptor, 12-bit offset, 4-bit length.
				return 1 + 12 + 4;
			}
		}
		// Given an edge, computes how many bits are used in the descriptor field.
		constexpr static size_t desc_bits(AdjListNode const &edge) noexcept {
			// Saxman always uses a single bit descriptor.
			ignore_unused_variable_warning(edge);
			return 1;
		}
		// Saxman allows encoding of a sequence of zeroes with no previous match.
		static void extra_matches(stream_t const *data, size_t basenode,
			                      size_t ubound, size_t lbound,
			                      LZSSGraph<SaxmanAdaptor>::MatchVector &matches) noexcept {
			ignore_unused_variable_warning(lbound);
			// Can't encode zero match after this point.
			if (basenode >= 0xFFF) {
				return;
			}
			// Try matching zeroes.
			size_t jj = 0;
			while (data[basenode + jj] == 0) {
				if (++jj >= ubound) {
					break;
				}
			}
			// Need at least 3 zeroes in sequence.
			if (jj >= 3) {
				// Got them, so add them to the list.
				matches[jj - 1] = AdjListNode(basenode + jj,
					                          numeric_limits<size_t>::max(),
					                          jj, 1 + 12 + 4);
			}
		}
		// Saxman needs no additional padding at the end-of-file.
		constexpr static size_t get_padding(size_t totallen) noexcept {
			ignore_unused_variable_warning(totallen);
			return 0;
		}
	};

	using SaxGraph = LZSSGraph<SaxmanAdaptor>;
	using SaxOStream = LZSSOStream<SaxmanAdaptor>;
	using SaxIStream = LZSSIStream<SaxmanAdaptor>;

public:
	static void decode(istream &in, iostream &Dst, size_t const Size) {
		SaxIStream src(in);

		// Loop while the file is good and we haven't gone over the declared length.
		while (in.good() && size_t(in.tellg()) < Size) {
			if (src.descbit() != 0u) {
				// Symbolwise match.
				if (in.peek() == EOF) {
					break;
				}
				Write1(Dst, src.getbyte());
			} else {
				if (in.peek() == EOF) {
					break;
				}
				// Dictionary match.
				// Offset and length of match.
				size_t offset = src.getbyte();
				size_t length = src.getbyte();

				// The high 4 bits of length are actually part of the offset.
				offset |= (length << 4) & 0xF00;
				// Length is low 4 bits plus 3.
				length = (length & 0xF) + 3;
				// And there is an additional 0x12 bytes added to offset.
				offset = (offset + 0x12) & 0xFFF;
				// The offset is stored as being absolute within current 0x1000-byte
				// block, with part of it being remapped to the end of the previous
				// 0x1000-byte block. We just rebase it around basedest.
				size_t basedest = Dst.tellp();
				offset = ((offset - basedest) & 0x0FFF) + basedest - 0x1000;

				if (offset < basedest) {
					// If the offset is before the current output position, we copy
					// bytes from the given location.
					for (size_t src = offset; src < offset + length; src++) {
						size_t Pointer = Dst.tellp();
						Dst.seekg(src);
						unsigned char Byte = Read1(Dst);
						Dst.seekp(Pointer);
						Write1(Dst, Byte);
					}
				} else {
					// Otherwise, it is a zero fill.
					for (size_t ii = 0; ii < length; ii++) {
						Write1(Dst, 0);
					}
				}
			}
		}
	}

	static void encode(ostream &Dst, unsigned char const *&Data, size_t const Size) {
		// Compute optimal Saxman parsing of input file.
		SaxGraph enc(Data, Size);
		SaxGraph::AdjList list = enc.find_optimal_parse();
		SaxOStream out(Dst);

		size_t pos = 0;
		// Go through each edge in the optimal path.
		for (SaxGraph::AdjList::const_iterator it = list.begin();
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
				size_t low = pos - dist, high = len;
				low = (low - 0x12) & 0xFFF;
				high = ((high - 3) & 0x0F) | ((low >> 4) & 0xF0);
				low &= 0xFF;
				out.putbyte(low);
				out.putbyte(high);
			}
			// Go to next position.
			pos = edge.get_dest();
		}
	}
};

bool saxman::decode(istream &Src, iostream &Dst, size_t Size) {
	if (Size == 0) {
		Size = LittleEndian::Read2(Src);
	}

	size_t Location = Src.tellg();
	stringstream in(ios::in | ios::out | ios::binary);
	extract(Src, in);

	saxman_internal::decode(in, Dst, Size);
	Src.seekg(Location + in.tellg());
	return true;
}

bool saxman::encode(ostream &Dst, unsigned char const *data, size_t const Size, bool const WithSize) {
	stringstream outbuff(ios::in | ios::out | ios::binary);
	saxman_internal::encode(outbuff, data, Size);
	if (WithSize) {
		outbuff.seekg(0, ios::end);
		LittleEndian::Write2(Dst, outbuff.tellg());
	}
	outbuff.seekg(0);
	Dst << outbuff.rdbuf();
	return true;
}
