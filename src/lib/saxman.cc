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
#include <iostream>
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
		enum class EdgeType : size_t {
			invalid,
			symbolwise,
			dictionary,
			zerofill
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
		// Flag that marks the descriptor bits as being in little-endian bit
		// order (that is, lowest bits come out first).
		constexpr static bool const DescriptorLittleEndianBits = true;
		// Size of the search buffer.
		constexpr static size_t const SearchBufSize = 4096;
		// Size of the look-ahead buffer.
		constexpr static size_t const LookAheadBufSize = 18;
		// Total size of the sliding window.
		constexpr static size_t const SlidingWindowSize = SearchBufSize + LookAheadBufSize;
		// Computes the type of edge that covers all of the "len" vertices starting from
		// "off" vertices ago.
		// Returns EdgeType::invalid if there is no such edge.
		constexpr static EdgeType match_type(size_t const dist, size_t const len) noexcept {
			// Preconditions:
			// len >= 1 && len <= LookAheadBufSize && dist != 0 && dist <= SearchBufSize
			ignore_unused_variable_warning(dist);
			if (len == 1) {
				return EdgeType::symbolwise;
			} else if (len == 2) {
				return EdgeType::invalid;
			} else {
				return EdgeType::dictionary;
			}
		}
		// Given an edge type, computes how many bits are used in the descriptor field.
		constexpr static size_t desc_bits(EdgeType const type) noexcept {
			// Saxman always uses a single bit descriptor.
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
				case EdgeType::zerofill:
					// 12-bit offset, 4-bit length.
					return desc_bits(type) + 12 + 4;
				default:
					return numeric_limits<size_t>::max();
			}
		}
		// Saxman allows encoding of a sequence of zeroes with no previous match.
		constexpr static void extra_matches(stream_t const *data, size_t const basenode,
			                                size_t const ubound, size_t const lbound,
			                                LZSSGraph<SaxmanAdaptor>::MatchVector &matches) noexcept {
			ignore_unused_variable_warning(lbound);
			// Can't encode zero match after this point.
			if (basenode >= SearchBufSize-1) {
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
				EdgeType const ty = EdgeType::zerofill;
				matches[jj - 1] = AdjListNode<EdgeType>(basenode + jj,
					                          numeric_limits<size_t>::max(),
					                          jj, edge_weight(ty), ty);
			}
		}
		// Saxman needs no additional padding at the end-of-file.
		constexpr static size_t get_padding(size_t const totallen) noexcept {
			ignore_unused_variable_warning(totallen);
			return 0;
		}
	};

public:
	static void decode(istream &in, iostream &Dst, size_t const Size) {
		using SaxIStream = LZSSIStream<SaxmanAdaptor>;

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
				size_t const basedest = Dst.tellp();
				offset = ((offset - basedest) % SaxmanAdaptor::SearchBufSize) + basedest - SaxmanAdaptor::SearchBufSize;

				if (offset < basedest) {
					// If the offset is before the current output position, we copy
					// bytes from the given location.
					for (size_t src = offset; src < offset + length; src++) {
						size_t const Pointer = Dst.tellp();
						Dst.seekg(src);
						unsigned char const Byte = Read1(Dst);
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
		using EdgeType = typename SaxmanAdaptor::EdgeType;
		using SaxGraph = LZSSGraph<SaxmanAdaptor>;
		using SaxOStream = LZSSOStream<SaxmanAdaptor>;

		// Compute optimal Saxman parsing of input file.
		SaxGraph enc(Data, Size);
		SaxGraph::AdjList list = enc.find_optimal_parse();
		SaxOStream out(Dst);

		size_t pos = 0;
		// Go through each edge in the optimal path.
		for (auto const &edge : list) {
			switch (edge.get_type()) {
				case EdgeType::symbolwise:
					out.descbit(1);
					out.putbyte(Data[pos]);
					break;
				case EdgeType::dictionary:
				case EdgeType::zerofill: {
					size_t const len  = edge.get_length(),
					             dist = edge.get_distance();
					out.descbit(0);
					size_t low = pos - dist, high = len;
					low = (low - 0x12) & 0xFFF;
					high = ((high - 3) & 0x0F) | ((low >> 4) & 0xF0);
					low &= 0xFF;
					out.putbyte(low);
					out.putbyte(high);
					break;
				}
				default:
					// This should be unreachable.
					std::cerr << "Compression produced invalid edge type " << static_cast<size_t>(edge.get_type()) << std::endl;
					__builtin_unreachable();
			};
			pos = edge.get_dest();
		}
	}
};

bool saxman::decode(istream &Src, iostream &Dst, size_t Size) {
	if (Size == 0) {
		Size = LittleEndian::Read2(Src);
	}

	size_t const Location = Src.tellg();
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
