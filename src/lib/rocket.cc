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
#include <type_traits>

#include "rocket.hh"
#include "bigendian_io.hh"
#include "bitstream.hh"
#include "ignore_unused_variable_warning.hh"
#include "lzss.hh"

using namespace std;

template<>
size_t moduled_rocket::PadMaskBits = 1u;

class rocket_internal {
	// NOTE: This has to be changed for other LZSS-based compression schemes.
	struct RocketAdaptor {
		using stream_t = uint8_t;
		using descriptor_t = uint8_t;
		using descriptor_endian_t = LittleEndian;
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
		// one single symbol.
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
		// Rocket assumes there is a string of 0x20's of lenght 0x3C0 before the
		// decompressed stream which can be used for overlapping matches.
		static void extra_matches(stream_t const *data,
			                      size_t const basenode,
			                      size_t const ubound, size_t const lbound,
			                      LZSSGraph<RocketAdaptor>::MatchVector &matches) noexcept {
			using Node_t = LZSSGraph<RocketAdaptor>::Node_t;
			ignore_unused_variable_warning(lbound);
			// Can't encode zero match after this point.
			if (basenode >= SearchBufSize-1) {
				return;
			}
			using diff_t = make_signed_t<size_t>;
			// For c++17, make this lambda constexpr so that the function can be also.
			// Its purpose is to pretend that there is a stream of 0x20's before the
			// start of data which can be used for normal LZSS matches.
			auto getValue = [&data](diff_t pos){
					if (pos >= 0) {
						return data[pos];
					} else {
						return stream_t(0x20);
					}
				};
			diff_t const base = diff_t(basenode);
			diff_t ii = base - 1;
			diff_t const slbound = basenode >= LookAheadBufSize ?
			                       base - SearchBufSize : diff_t(LookAheadBufSize - SearchBufSize);
			diff_t const subound = diff_t(ubound) - base;
			do {
				// Keep looking for dictionary matches.
				diff_t jj = 0;
				while (getValue(ii + jj) == data[base + jj]) {
					++jj;
					// We have found a match that links (basenode) with
					// (basenode + jj) with length (jj) and distance (basenode-ii).
					// Add it to the list if it is a better match.
					EdgeType const ty = match_type(basenode - ii, jj);
					if (ty != EdgeType::invalid) {
						size_t const wgt = edge_weight(ty);
						Node_t &best = matches[jj - 1];
						if (wgt < best.get_weight()) {
							best = Node_t(basenode, basenode - ii, jj, wgt, ty);
						}
					}
					// We can find no more matches with the current starting node.
					if (jj >= subound) {
						break;
					}
				}
			} while (ii-- > slbound);
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
		using diff_t = make_signed_t<size_t>;

		RockIStream src(in);

		auto getValue = [&Dst](diff_t src){
				if (src >= 0) {
					diff_t const Pointer = diff_t(Dst.tellp());
					Dst.seekg(src);
					RocketAdaptor::stream_t const Byte = Read1(Dst);
					Dst.seekp(Pointer);
					return Byte;
				} else {
					return RocketAdaptor::stream_t(0x20);
				}
			};

		while (in.good() && in.tellg() < Size) {
			if (src.descbit() != 0u) {
				// Symbolwise match.
				uint8_t const Byte = Read1(in);
				Write1(Dst, Byte);
			} else {
				// Dictionary match.
				// Distance and length of match.
				diff_t const high = src.getbyte(),
					         low  = src.getbyte();
				diff_t const length = ((high & 0xFC) >> 2) + 1u;
				diff_t offset = ((high&3)<<8)|low;
				// The offset is stored as being absolute within a 0x400-byte buffer,
				// starting at position 0x3C0. We just rebase it around basedest + 0x3C0u.
				constexpr diff_t const bias = diff_t(RocketAdaptor::SearchBufSize - RocketAdaptor::LookAheadBufSize);
				diff_t const basedest = diff_t(Dst.tellp());
				offset = diff_t(((offset - basedest - bias) % RocketAdaptor::SearchBufSize) + basedest - RocketAdaptor::SearchBufSize);

				for (diff_t src = offset; src < offset + length; src++) {
					Write1(Dst, getValue(src));
				}
			}
		}
	}

	static void encode(ostream &Dst, uint8_t const *&Data, size_t const Size) {
		using EdgeType = typename RocketAdaptor::EdgeType;
		using RockGraph = LZSSGraph<RocketAdaptor>;
		using RockOStream = LZSSOStream<RocketAdaptor>;

		// Compute optimal Rocket parsing of input file.
		RockGraph enc(Data, Size);
		RockGraph::AdjList list = enc.find_optimal_parse();
		RockOStream out(Dst);

		// Go through each edge in the optimal path.
		for (auto const &edge : list) {
			switch (edge.get_type()) {
				case EdgeType::symbolwise:
					out.descbit(1);
					out.putbyte(edge.get_symbol());
					break;
				case EdgeType::dictionary: {
					constexpr size_t const bias = RocketAdaptor::SearchBufSize - RocketAdaptor::LookAheadBufSize;
					size_t const len  = edge.get_length(),
					             dist = edge.get_distance(),
					             pos  = (edge.get_pos() + bias - dist) % RocketAdaptor::SearchBufSize;
					out.descbit(0);
					out.putbyte(((len-1)<<2)|(pos>>8));
					out.putbyte(pos);
					break;
				}
				default:
					// This should be unreachable.
					std::cerr << "Compression produced invalid edge type " << static_cast<size_t>(edge.get_type()) << std::endl;
					__builtin_unreachable();
			};
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

bool rocket::encode(ostream &Dst, uint8_t const *data, size_t const Size) {
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
