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

#ifndef __LIB_LZSS_H
#define __LIB_LZSS_H

#include <algorithm>
#include <iosfwd>
#include <limits>
#include <list>
#include <string>
#include <vector>

#include "bigendian_io.h"
#include "bitstream.h"

#ifdef _MSC_VER
#ifndef __clang__
[[noreturn]] inline void __builtin_unreachable() {
	__assume(false);
}
#endif
#endif

/*
 * Class representing an edge in the LZSS-compression graph. An edge (u, v)
 * indicates that there is a sliding window match that covers all the characters
 * in the [u, v) range (half-open) -- that is, node v is not part of the match.
 * Each node is a character in the file, and is represented by its position.
 */
template <typename EdgeType>
class AdjListNode {
private:
	// The first character after the match ends.
	size_t destnode;
	// Cost, in bits, of "covering" all of the characters in the match.
	size_t weight;
	// How many characters back does the match begin at.
	size_t distance;
	// How long the match is.
	size_t length;
	EdgeType type;
public:
	// Constructors.
	constexpr AdjListNode() noexcept
		: destnode(1), weight(std::numeric_limits<size_t>::max()), distance(1),
		  length(1), type(EdgeType::symbolwise) {
	}
	constexpr AdjListNode(size_t dest, size_t dist, size_t len, size_t wgt, EdgeType ty) noexcept
		: destnode(dest), weight(wgt), distance(dist), length(len), type(ty) {
	}
	constexpr AdjListNode(AdjListNode<EdgeType> const &other) noexcept = default;
	constexpr AdjListNode(AdjListNode<EdgeType> &&other) noexcept = default;
	constexpr AdjListNode &operator=(AdjListNode<EdgeType> const &other) noexcept = default;
	constexpr AdjListNode &operator=(AdjListNode<EdgeType> &&other) noexcept = default;
	// Getters.
	constexpr size_t get_dest() const noexcept {
		return destnode;
	}
	constexpr size_t get_weight() const noexcept {
		return weight;
	}
	constexpr size_t get_distance() const noexcept {
		return distance;
	}
	constexpr size_t get_length() const noexcept {
		return length;
	}
	constexpr EdgeType get_type() const noexcept {
		return type;
	}
	// Comparison operator. Lowest weight first, on tie, break by shortest
	// length, on further tie break by distance. Used only on the multimap.
	constexpr bool operator<(AdjListNode<EdgeType> const &other) const noexcept {
		if (weight < other.weight) {
			return true;
		} else if (weight > other.weight) {
			return false;
		}
		if (length < other.length) {
			return true;
		} else if (length > other.length) {
			return false;
		} else {
			return distance < other.distance;
		}
	}
};

/*
 * Graph structure for optimal LZSS encoding. This graph is a directed acyclic
 * graph (DAG) by construction, and is automatically sorted topologically.
 * The template parameter is an adaptor class/structure with the following
 * members:
 *  struct LZSSAdaptor {
 *  	using stream_t     = unsigned char;
 *  	using descriptor_t = unsigned short;
 *  	using descriptor_endian_t = littleendian<descriptor_t>;
 *  	enum class EdgeType : size_t {
 *  		invalid,
 *  		// other cases
 *  	};
 *  	constexpr static size_t const NumDescBits = sizeof(descriptor_t) * 8;
 *  	// Number of bits used in descriptor bitfield to signal the end-of-file
 *  	// marker sequence.
 *  	constexpr static size_t const NumTermBits = 2;
 *  	// Flag that tells the compressor that new descriptor fields are needed
 *  	// as soon as the last bit in the previous one is used up.
 *  	constexpr static bool const NeedEarlyDescriptor = true;
 *  	// Flag that marks the descriptor bits as being in little-endian bit
 *  	// order (that is, lowest bits come out first).
 *  	constexpr static bool const DescriptorLittleEndianBits = true;
 *  	// Size of the search buffer.
 *  	constexpr static size_t const SearchBufSize = 8192;
 *  	// Size of the look-ahead buffer.
 *  	constexpr static size_t const LookAheadBufSize = 256;
 *  	// Total size of the sliding window.
 *  	constexpr static size_t const SlidingWindowSize = SearchBufSize + LookAheadBufSize;
 *  	// Computes the type of edge that covers all of the "len" vertices starting from
 *  	// "off" vertices ago.
 *  	// Returns EdgeType::invalid if there is no such edge.
 *  	constexpr static EdgeType match_type(size_t const dist, size_t const len) noexcept
 *  	// Given an edge type, computes how many bits are used in the descriptor field.
 *  	constexpr static size_t desc_bits(EdgeType const type) noexcept;
 *  	// Given an edge type, computes how many bits are used in total by this edge.
 *  	// A return of "numeric_limits<size_t>::max()" means "infinite",
 *  	// or "no edge".
 *  	constexpr static size_t edge_weight(EdgeType const type) noexcept;
 *  	// Function that finds extra matches in the data that are specific to the
 *  	// given encoder and not general LZSS dictionary matches. May be constexpr.
 *  	static void extra_matches(stream_t const *data, size_t const basenode,
 *  	                          size_t const ubound, size_t const lbound,
 *  	                          LZSSGraph<KosinskiAdaptor>::MatchVector &matches) noexcept;
 *  	// Function that computes padding between modules, if any. May be constexpr.
 *  	static size_t get_padding(size_t const totallen) noexcept;
 */
template<typename Adaptor>
class LZSSGraph {
public:
	using EdgeType = typename Adaptor::EdgeType;
	using Node_t = AdjListNode<EdgeType>;
	using AdjList = std::list<Node_t>;
	using MatchVector = std::vector<Node_t>;
private:
	// Source file data and its size; one node per character in source file.
	typename Adaptor::stream_t const *data;
	size_t const nlen;
	// Adjacency lists for all the nodes in the graph.
	std::vector<AdjList> adjs;

	/*
	 * TODO: Improve speed with a smarter way to perform matches.
	 * This is the main workhorse and bottleneck: it finds the least costly way
	 * to reach all possible nodes reachable from the basenode and inserts them
	 * into a map.
	 */
	MatchVector find_matches(size_t basenode) const noexcept {
		static_assert(noexcept(Adaptor::edge_weight(EdgeType())),
		                       "Adaptor::edge_weight() is not noexcept");
		static_assert(noexcept(Adaptor::match_type(basenode, basenode)),
		                       "Adaptor::match_type() is not noexcept");
		// Upper and lower bounds for sliding window, starting node.
		size_t const ubound = std::min(size_t(Adaptor::LookAheadBufSize), nlen - basenode),
		             lbound = basenode > Adaptor::SearchBufSize ? basenode - Adaptor::SearchBufSize : 0;
		size_t ii = basenode - 1;
		// This is what we produce.
		MatchVector matches(ubound);
		// Start with the literal/symbolwise encoding of the current node.
		EdgeType const ty = Adaptor::match_type(0, 1);
		matches[0] = Node_t(basenode + 1, 0, 1, Adaptor::edge_weight(ty), ty);
		// Get extra dictionary matches dependent on specific encoder.
		static_assert(noexcept(Adaptor::extra_matches(data, basenode, ubound, lbound, matches)),
		                       "Adaptor::extra_matches() is not noexcept");
		Adaptor::extra_matches(data, basenode, ubound, lbound, matches);
		// First node is special.
		if (basenode == 0) {
			return matches;
		}
		do {
			// Keep looking for dictionary matches.
			size_t jj = 0;
			while (data[ii + jj] == data[basenode + jj]) {
				++jj;
				// We have found a match that links (basenode) with
				// (basenode + jj) with length (jj) and distance (basenode-ii).
				// Add it to the list if it is a better match.
				EdgeType const ty = Adaptor::match_type(basenode - ii, jj);
				if (ty != EdgeType::invalid) {
					size_t const wgt = Adaptor::edge_weight(ty);
					Node_t &best = matches[jj - 1];
					if (wgt < best.get_weight()) {
						best = Node_t(basenode + jj, basenode - ii, jj, wgt, ty);
					}
				}
				// We can find no more matches with the current starting node.
				if (jj >= ubound) {
					break;
				}
			}
		} while (ii-- > lbound);

		return matches;
	}
public:
	// Constructor: creates the graph from the input file.
	LZSSGraph(unsigned char const *dt, size_t const size) noexcept
		: data(reinterpret_cast<typename Adaptor::stream_t const *>(dt)),
		  nlen(size / sizeof(typename Adaptor::stream_t)) {
		// Making space for all nodes.
		adjs.resize(nlen);
		for (size_t ii = 0; ii < nlen; ii++) {
			// Find all matches for all subsequent nodes.
			MatchVector const matches = find_matches(ii);
			for (const auto & match : matches) {
				// Insert the best (lowest cost) edge linking these two nodes.
				if (match.get_weight() != std::numeric_limits<size_t>::max()) {
					adjs[ii].push_back(match);
				}
			}
		}
	}
	/*
	 * This function returns the shortest path through the file.
	 */
	AdjList find_optimal_parse() const noexcept {
		static_assert(noexcept(Adaptor::desc_bits(EdgeType())),
		                       "Adaptor::desc_bits() is not noexcept");
		static_assert(noexcept(Adaptor::get_padding(0)),
		                       "Adaptor::get_padding() is not noexcept");
		// Auxiliary data structures:
		// * The parent of a node is the node that reaches that node with the
		//   lowest cost from the start of the file.
		std::vector<size_t> parents(nlen + 1);
		// * This is the edge used to go from the parent of a node to said node.
		std::vector<Node_t> pedges(nlen + 1);
		// * This is the total cost to reach the edge. They start as high as
		//   possible for all nodes but the first, which starts at 0.
		std::vector<size_t> costs(nlen + 1, std::numeric_limits<size_t>::max());
		costs[0] = 0;
		// * And this is a vector that tallies up the amount of bits in
		//   the descriptor bitfield for the shortest path up to this node.
		//   After tallying up the ending node, the end-of-file marker may cause
		//   an additional dummy descriptor bitfield to be emitted; this vector
		//   is used to counteract that.
		std::vector<size_t> desccosts(nlen + 1, std::numeric_limits<size_t>::max());
		desccosts[0] = 0;

		// Since the LZSS graph is a topologically-sorted DAG by construction,
		// computing the shortest distance is very quick and easy: just go
		// through the nodes in order and update the distances.
		for (size_t ii = 0; ii < nlen; ii++) {
			// Get the adjacency list for this node.
			AdjList const &list = adjs[ii];
			// Get remaining unused descriptor bits up to this node.
			size_t const basedesc = desccosts[ii];
			for (const auto & elem : list) {
				// Need destination ID and edge weight.
				size_t const nextnode = elem.get_dest();
				size_t wgt = costs[ii] + elem.get_weight();
				// Compute descriptor bits from using this edge.
				size_t desccost = basedesc + Adaptor::desc_bits(elem.get_type());
				if (nextnode == nlen) {
					// This is the ending node. Add the descriptor bits for the
					// end-of-file marker.
					wgt += Adaptor::NumTermBits;
					desccost += Adaptor::NumTermBits;
					// If the descriptor bitfield had exactly 0 bits left after
					// this, we may need to emit a new descriptor bitfield (the
					// full Adaptor::NumDescBits bits). Otherwise, we need to
					// pads the last descriptor bitfield to full size. This line
					// accomplishes both.
					size_t const descmod = desccost % Adaptor::NumDescBits;
					if (descmod != 0 || Adaptor::NeedEarlyDescriptor) {
						wgt += (Adaptor::NumDescBits - descmod);
						desccost += (Adaptor::NumDescBits - descmod);
					}
					// Compensate for the Adaptor's padding, if any. 
					wgt += Adaptor::get_padding(wgt);
				}
				// Is the cost to reach the target node through this edge less
				// than the current cost?
				if (costs[nextnode] > wgt) {
					// If so, update the data structures with new best edge.
					costs[nextnode] = wgt;
					parents[nextnode] = ii;
					pedges[nextnode] = elem;
					desccosts[nextnode] = desccost;
				}
			}
		}

		// This is what we will produce.
		AdjList parselist;
		for (size_t ii = nlen; ii != 0;) {
			// Insert the edge up front...
			parselist.push_front(pedges[ii]);
			// ... and switch to parent node.
			ii = parents[ii];
		}

		// We are done: this is the optimal parsing of the input file, giving
		// *the* best possible compressed file size.
		return parselist;
	}
};

/*
 * This class abstracts away an LZSS output stream composed of one or more bytes
 * in a descriptor bitfield, followed by byte parameters. It manages the output
 * by buffering the bytes until a descriptor field is full, at which point it
 * writes the descriptor field and flushes the output buffer.
 */
template <typename Adaptor>
class LZSSOStream {
private:
	using descriptor_t = typename Adaptor::descriptor_t;
	using BitWriter = typename Adaptor::descriptor_endian_t;
	// Where we will output to.
	std::ostream &out;
	// Internal bitstream output buffer.
	obitstream<descriptor_t, Adaptor::DescriptorLittleEndianBits, BitWriter> bits;
	// Internal parameter buffer.
	std::string buffer;
	void flushbuffer() noexcept {
		out.write(buffer.c_str(), buffer.size());
		buffer.clear();
	}

public:
	// Constructor.
	LZSSOStream(std::ostream &Dst) noexcept : out(Dst), bits(out) {
	}
	// Destructor: writes anything that hasn't been written.
	~LZSSOStream() noexcept {
		// We need a dummy descriptor field if we have exactly zero bits left
		// on the previous descriptor field; this is because the decoder will
		// immediately fetch a new descriptor field when the previous one has
		// expired, and we don't want it to be the terminating sequence.
		// First, save current state.
		bool const needdummydesc = !bits.have_waiting_bits();
		// Now, flush the queue if needed.
		bits.flush();
		if (Adaptor::NeedEarlyDescriptor && needdummydesc) {
			// We need to add a dummy descriptor field; so add it.
			for (size_t ii = 0; ii < sizeof(descriptor_t); ii++) {
				out.put(0x00);
			}
		}
		// Now write the terminating sequence if it wasn't written already.
		flushbuffer();
	}
	// Writes a bit to the descriptor bitfield. When the descriptor field is
	// full, outputs it and the output parameter buffer.
	void descbit(descriptor_t const bit) noexcept {
		if (Adaptor::NeedEarlyDescriptor) {
			if (bits.push(bit)) {
				flushbuffer();
			}
		} else {
			if (!bits.have_waiting_bits()) {
				flushbuffer();
			}
			bits.push(bit);
		}
	}
	// Puts a byte in the output buffer.
	void putbyte(size_t const c) noexcept {
		Write1(buffer, c);
	}
};

/*
 * This class abstracts away an LZSS input stream composed of one or more bytes
 * in a descriptor bitfield, followed by byte parameters. It manages the input
 * by reading a descriptor field when one is required (as defined by the adaptor
 * class), so that bytes can be read when needed from the input stream.
 */
template <typename Adaptor>
class LZSSIStream {
private:
	using descriptor_t = typename Adaptor::descriptor_t;
	using BitWriter = typename Adaptor::descriptor_endian_t;
	// Where we will input to.
	std::istream &in;
	// Internal bitstream input buffer.
	ibitstream<descriptor_t, Adaptor::NeedEarlyDescriptor,
	 Adaptor::DescriptorLittleEndianBits, BitWriter> bits;
	// Internal parameter buffer.
	std::string buffer;
public:
	// Constructor.
	LZSSIStream(std::istream &Src) noexcept : in(Src), bits(in) {
	}
	// Destructor: writes anything that hasn't been written.
	~LZSSIStream() noexcept {
	}
	// Writes a bit to the descriptor bitfield. When the descriptor field is
	// full, inputs it and the input parameter buffer.
	descriptor_t descbit() noexcept {
		return bits.pop();
	}
	// Puts a byte in the input buffer.
	unsigned char getbyte() noexcept {
		return Read1(in);
	}
};

#endif // __LIB_LZSS_H
