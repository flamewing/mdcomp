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

#ifndef LIB_LZSS_HH
#define LIB_LZSS_HH

#include <mdcomp/bigendian_io.hh>
#include <mdcomp/bitstream.hh>

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <list>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#ifdef _MSC_VER
#    ifndef __clang__
[[noreturn]] inline void __builtin_unreachable() {
    __assume(false);
}
#    endif
#endif

/*
 * Class representing an edge in the LZSS-compression graph. An edge (u, v)
 * indicates that there is a sliding window match that covers all the characters
 * in the [u, v) range (half-open) -- that is, node v is not part of the match.
 * Each node is a character in the file, and is represented by its position.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

template <typename Adaptor>
class AdjListNode {
public:
    using EdgeType = typename Adaptor::EdgeType;
    using stream_t = typename Adaptor::stream_t;

    struct MatchInfo {
        // How many characters back does the match begin at.
        size_t distance;
        // How long the match is.
        size_t length;
    };

private:
    // The first character after the match ends.
    size_t currpos{0};
    // Cost, in bits, of "covering" all of the characters in the match.
    EdgeType type;

    union {
        MatchInfo match;
        stream_t  symbol;
    };

public:
    // Constructors.
    constexpr AdjListNode() noexcept : type(EdgeType::invalid), symbol(stream_t(0)) {}

    constexpr AdjListNode(size_t pos, stream_t sym, EdgeType type_) noexcept
            : currpos(pos), type(type_), symbol(sym) {}

    constexpr AdjListNode(size_t pos, MatchInfo match_, EdgeType type_) noexcept
            : currpos(pos), type(type_), match(match_) {}

    // Getters.
    [[nodiscard]] constexpr size_t get_pos() const noexcept {
        return currpos;
    }

    [[nodiscard]] constexpr size_t get_dest() const noexcept {
        return currpos + get_length();
    }

    [[nodiscard]] constexpr size_t get_weight() const noexcept {
        return Adaptor::edge_weight(type, get_length());
    }

    [[nodiscard]] constexpr size_t get_distance() const noexcept {
        switch (type) {
        case EdgeType::invalid:
        case EdgeType::terminator:
        case EdgeType::symbolwise:
            return 0;
        default:
            return match.distance;
        }
    }

    [[nodiscard]] constexpr size_t get_length() const noexcept {
        switch (type) {
        case EdgeType::invalid:
        case EdgeType::terminator:
        case EdgeType::symbolwise:
            return 1;
        default:
            return match.length;
        }
    }

    constexpr stream_t get_symbol() const noexcept {
        switch (type) {
        case EdgeType::symbolwise:
            return symbol;
        default:
            return std::numeric_limits<stream_t>::max();
        }
    }

    constexpr EdgeType get_type() const noexcept {
        return type;
    }
};

#pragma GCC diagnostic pop

template <typename Adaptor>
class SlidingWindow {
public:
    using EdgeType    = typename Adaptor::EdgeType;
    using stream_t    = typename Adaptor::stream_t;
    using Node_t      = AdjListNode<Adaptor>;
    using Match_t     = typename Node_t::MatchInfo;
    using MatchVector = std::vector<Node_t>;
    using Data_t      = std::span<stream_t const>;

    SlidingWindow(
            Data_t data_, size_t const bufsize, size_t const minmatch,
            size_t const labuflen, EdgeType const type_) noexcept
            : data(data_), srchbufsize(bufsize), minmatchlen(minmatch),
              basenode(Adaptor::FirstMatchPosition),
              ubound(std::min(labuflen + basenode, data.size())),
              lbound(basenode > srchbufsize ? basenode - srchbufsize : 0), type(type_) {}

    [[nodiscard]] size_t getDataSize() const {
        return data.size();
    }

    [[nodiscard]] size_t getSearchBufSize() const {
        return basenode - lbound;
    }

    [[nodiscard]] size_t getLookAheadBufSize() const {
        return ubound - basenode;
    }

    [[nodiscard]] size_t getWindowSize() const {
        return ubound - lbound;
    }

    bool slideWindow() noexcept {
        if (ubound != data.size()) {
            ubound++;
        }
        if (basenode != data.size()) {
            basenode++;
        }
        if (getSearchBufSize() > srchbufsize) {
            lbound++;
        }
        return getLookAheadBufSize() != 0;
    }

    void find_matches(MatchVector& matches) const noexcept {
        static_assert(
                noexcept(Adaptor::edge_weight(EdgeType(), size_t())),
                "Adaptor::edge_weight() is not noexcept");
        size_t const end = getLookAheadBufSize();
        // This is what we produce.
        matches.clear();
        // First node is special.
        if (getSearchBufSize() == 0) {
            return;
        }
        size_t base     = basenode - 1;
        size_t best_pos = 0;
        size_t best_len = 0;
        do {
            // Keep looking for dictionary matches.
            size_t offset = 0;
            while (offset < end && data[base + offset] == data[basenode + offset]) {
                ++offset;
            }
            if (best_len < offset) {
                best_pos = base;
                best_len = offset;
            }
            if (offset == end) {
                break;
            }
        } while (base-- > lbound);

        if (best_len >= minmatchlen) {
            // We have found a match that links (basenode) with
            // (basenode + best_len) with length (best_len) and distance
            // equal to (basenode-best_pos).
            // Add it, and all prefixes, to the list, as long as it is a better
            // match.
            for (size_t jj = minmatchlen; jj <= best_len; ++jj) {
                matches.emplace_back(basenode, Match_t{basenode - best_pos, jj}, type);
            }
        }
    }

    bool find_extra_matches(MatchVector& matches) const noexcept {
        static_assert(
                noexcept(Adaptor::extra_matches(
                        data, basenode, ubound, lbound, std::declval<MatchVector&>())),
                "Adaptor::extra_matches() is not noexcept");
        // This is what we produce.
        matches.clear();
        // Get extra dictionary matches dependent on specific encoder.
        return Adaptor::extra_matches(data, basenode, ubound, lbound, matches);
    }

private:
    // Source file data and its size; one node per character in source file.
    Data_t         data;
    size_t const   srchbufsize;
    size_t const   minmatchlen;
    size_t         basenode;
    size_t         ubound;
    size_t         lbound;
    EdgeType const type;
};

template <typename T>
concept LZSSAdaptor = requires() {
    std::unsigned_integral<typename T::stream_t>;
    !std::same_as<typename T::stream_t, bool>;
    std::is_class_v<typename T::stream_endian_t>;
    std::unsigned_integral<typename T::descriptor_t>;
    !std::same_as<typename T::descriptor_t, bool>;
    std::is_class_v<typename T::descriptor_endian_t>;
    std::is_enum_v<typename T::EdgeType>;
    { T::EdgeType::invalid } -> std::same_as<typename T::EdgeType>;
    { T::EdgeType::terminator } -> std::same_as<typename T::EdgeType>;
    { T::EdgeType::symbolwise } -> std::same_as<typename T::EdgeType>;
    std::same_as<decltype(T::NumDescBits), size_t const>;
    std::same_as<decltype(T::NeedEarlyDescriptor), bool const>;
    std::same_as<decltype(T::DescriptorBitOrder), bit_endian>;
    std::same_as<decltype(T::FirstMatchPosition), size_t const>;
    std::same_as<decltype(T::SearchBufSize), size_t const>;
    std::same_as<decltype(T::LookAheadBufSize), size_t const>;
    requires requires(
            uint8_t const*& rptr, uint8_t*& wptr, std::istream& input,
            std::ostream& output, typename T::stream_t stream_val,
            typename T::stream_endian_t     stream_endian,
            typename T::descriptor_t        descriptor_val,
            typename T::descriptor_endian_t descriptor_endian) {
        {
            decltype(stream_endian)::template Read<decltype(stream_val)>(rptr)
            } -> std::same_as<decltype(stream_val)>;
        {
            decltype(stream_endian)::template Read<decltype(stream_val)>(input)
            } -> std::same_as<decltype(stream_val)>;
        { decltype(stream_endian)::Write(wptr, stream_val) } -> std::same_as<void>;
        { decltype(stream_endian)::Write(output, stream_val) } -> std::same_as<void>;
        {
            decltype(descriptor_endian)::template Read<decltype(descriptor_val)>(rptr)
            } -> std::same_as<decltype(descriptor_val)>;
        {
            decltype(descriptor_endian)::template Read<decltype(descriptor_val)>(input)
            } -> std::same_as<decltype(descriptor_val)>;
        {
            decltype(descriptor_endian)::Write(wptr, descriptor_val)
            } -> std::same_as<void>;
        {
            decltype(descriptor_endian)::Write(output, descriptor_val)
            } -> std::same_as<void>;
    };
    requires requires(
            typename T::EdgeType type, size_t value, size_t lbound,
            std::vector<AdjListNode<T>>           nodes,
            std::span<typename T::stream_t const> data) {
        {T::create_sliding_window(data)};
        { T::desc_bits(type) } -> std::same_as<size_t>;
        { T::edge_weight(type, value) } -> std::same_as<size_t>;
        { T::extra_matches(data, value, value, value, nodes) } -> std::same_as<bool>;
        { T::get_padding(value) } -> std::same_as<size_t>;
        noexcept(T::desc_bits(type));
        noexcept(T::edge_weight(type, value));
        noexcept(T::get_padding(value));
        noexcept(T::extra_matches(data, value, value, value, nodes));
    };
};

/*
 * Function which creates a LZSS structure and finds the optimal parse.
 */

template <typename AdjList>
struct lzss_parse_result {
    AdjList parse_list;
    size_t  desc_size;
    size_t  file_size;
};

template <LZSSAdaptor Adaptor>
auto find_optimal_lzss_parse(
        uint8_t const* data_, size_t const size, Adaptor adaptor) noexcept {
    ignore_unused_variable_warning(adaptor);
    using EdgeType        = typename Adaptor::EdgeType;
    using stream_t        = typename Adaptor::stream_t;
    using stream_endian_t = typename Adaptor::stream_endian_t;
    using Node_t          = AdjListNode<Adaptor>;
    using AdjList         = std::list<Node_t>;
    using MatchVector     = std::vector<Node_t>;
    using Data_t          = std::span<stream_t const>;

    auto read_stream = [](uint8_t const*& ptr) {
        return stream_endian_t::template Read<stream_t>(ptr);
    };

    // Adjacency lists for all the nodes in the graph.
    stream_t const* const data{reinterpret_cast<stream_t const*>(data_)};
    size_t const          nlen{size / sizeof(stream_t)};
    static_assert(
            noexcept(Adaptor::desc_bits(EdgeType())),
            "Adaptor::desc_bits() is not noexcept");
    static_assert(
            noexcept(Adaptor::get_padding(0)), "Adaptor::get_padding() is not noexcept");
    auto assume = [](bool result) {
        if (!result) {
            __builtin_unreachable();
        }
    };
    assume(nlen >= Adaptor::FirstMatchPosition);
    size_t numNodes = nlen - Adaptor::FirstMatchPosition;
    assume(numNodes <= std::numeric_limits<size_t>::max() - 1);
    // Auxiliary data structures:
    // * The parent of a node is the node that reaches that node with the
    //   lowest cost from the start of the file.
    std::vector<size_t> parents(numNodes + 1);
    // * This is the edge used to go from the parent of a node to said node.
    std::vector<Node_t> pedges(numNodes + 1);
    // * This is the total cost to reach the edge. They start as high as
    //   possible for all nodes but the first, which starts at 0.
    std::vector<size_t> costs(numNodes + 1, std::numeric_limits<size_t>::max());
    costs[0] = 0;
    // * And this is a vector that tallies up the amount of bits in
    //   the descriptor bitfield for the shortest path up to this node.
    //   After tallying up the ending node, the end-of-file marker may cause
    //   an additional dummy descriptor bitfield to be emitted; this vector
    //   is used to counteract that.
    std::vector<size_t> desccosts(numNodes + 1, std::numeric_limits<size_t>::max());
    desccosts[0] = 0;

    // Extracting distance relax logic from the loop so it can be used more
    // often.
    auto Relax = [lastnode = nlen, &costs, &desccosts, &parents,
                  &pedges](size_t index, size_t const basedesc, auto const& elem) {
        // Need destination ID and edge weight.
        size_t const nextnode = elem.get_dest() - Adaptor::FirstMatchPosition;
        size_t       wgt      = costs[index] + elem.get_weight();
        // Compute descriptor bits from using this edge.
        size_t desccost = basedesc + Adaptor::desc_bits(elem.get_type());
        if (nextnode == lastnode) {
            // This is the ending node. Add the descriptor bits for the
            // end-of-file marker.
            wgt += Adaptor::edge_weight(EdgeType::terminator, 0);
            desccost += Adaptor::desc_bits(EdgeType::terminator);
            // If the descriptor bitfield had exactly 0 bits left after
            // this, we may need to emit a new descriptor bitfield (the
            // full Adaptor::NumDescBits bits). Otherwise, we need to
            // pads the last descriptor bitfield to full size.
            // This accomplishes both.
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
            costs[nextnode]     = wgt;
            parents[nextnode]   = index;
            pedges[nextnode]    = elem;
            desccosts[nextnode] = desccost;
        }
    };

    // Since the LZSS graph is a topologically-sorted DAG by construction,
    // computing the shortest distance is very quick and easy: just go
    // through the nodes in order and update the distances.
    auto        winSet = Adaptor::create_sliding_window(Data_t(data, nlen));
    MatchVector matches;
    matches.reserve(Adaptor::LookAheadBufSize);
    for (size_t ii = 0; ii < numNodes; ii++) {
        // Get remaining unused descriptor bits up to this node.
        size_t const basedesc = desccosts[ii];
        // Start with the literal/symbolwise encoding of the current node.
        {
            size_t const   offset = ii + Adaptor::FirstMatchPosition;
            auto const*    ptr    = reinterpret_cast<uint8_t const*>(data + offset);
            stream_t const val    = read_stream(ptr);
            EdgeType const type   = EdgeType::symbolwise;
            Relax(ii, basedesc, Node_t(offset, val, type));
        }
        // Get the adjacency list for this node.
        for (auto& win : winSet) {
            if (!win.find_extra_matches(matches)) {
                win.find_matches(matches);
            }
            for (auto const& elem : matches) {
                if (elem.get_type() != EdgeType::invalid) {
                    Relax(ii, basedesc, elem);
                }
            }
            win.slideWindow();
        }
    }

    // This is what we will produce.
    lzss_parse_result<AdjList> result{
            {Node_t{0, 0, EdgeType::terminator}}, desccosts.back(), costs.back()};
    AdjList& parselist = result.parse_list;
    for (size_t ii = numNodes; ii != 0;) {
        // Insert the edge up front...
        parselist.push_front(pedges[ii]);
        // ... and switch to parent node.
        ii = parents[ii];
    }

    // We are done: this is the optimal parsing of the input file, giving
    // us *the* best possible compressed file size.
    return result;
}

/*
 * This class abstracts away an LZSS output stream composed of one or more bytes
 * in a descriptor bitfield, followed by byte parameters. It manages the output
 * by buffering the bytes until a descriptor field is full, at which point it
 * writes the descriptor field and flushes the output buffer.
 */
template <LZSSAdaptor Adaptor>
class LZSSOStream {
private:
    using descriptor_t        = typename Adaptor::descriptor_t;
    using descriptor_endian_t = typename Adaptor::descriptor_endian_t;
    using bitbuffer_t
            = obitstream<descriptor_t, Adaptor::DescriptorBitOrder, descriptor_endian_t>;
    // Where we will output to.
    std::ostream& out;
    // Internal bitstream output buffer.
    bitbuffer_t bits;
    // Internal parameter buffer.
    std::string buffer;

    void flushbuffer() noexcept {
        out.write(buffer.c_str(), buffer.size());
        buffer.clear();
    }

public:
    // Constructor.
    explicit LZSSOStream(std::ostream& Dst) noexcept : out(Dst), bits(out) {}

    LZSSOStream(LZSSOStream const&)                = delete;
    LZSSOStream(LZSSOStream&&) noexcept            = delete;
    LZSSOStream& operator=(LZSSOStream const&)     = delete;
    LZSSOStream& operator=(LZSSOStream&&) noexcept = delete;

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
        if constexpr (Adaptor::NeedEarlyDescriptor) {
            if (needdummydesc) {
                // We need to add a dummy descriptor field; so add it.
                descriptor_endian_t::Write(out, descriptor_t{0});
            }
        }
        // Now write the terminating sequence if it wasn't written already.
        flushbuffer();
    }

    // Writes a bit to the descriptor bitfield. When the descriptor field is
    // full, outputs it and the output parameter buffer.
    void descbit(descriptor_t const bit) noexcept {
        if constexpr (Adaptor::NeedEarlyDescriptor) {
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
    void putbyte(size_t const value) noexcept {
        Write1(buffer, value);
    }
};

/*
 * This class abstracts away an LZSS input stream composed of one or more bytes
 * in a descriptor bitfield, followed by byte parameters. It manages the input
 * by reading a descriptor field when one is required (as defined by the adaptor
 * class), so that bytes can be read when needed from the input stream.
 */
template <LZSSAdaptor Adaptor>
class LZSSIStream {
private:
    using descriptor_t        = typename Adaptor::descriptor_t;
    using descriptor_endian_t = typename Adaptor::descriptor_endian_t;
    using bitbuffer_t         = ibitstream<
            descriptor_t, Adaptor::DescriptorBitOrder, descriptor_endian_t,
            Adaptor::NeedEarlyDescriptor>;
    // Where we will input to.
    std::istream& in;
    // Internal bitstream input buffer.
    bitbuffer_t bits;

public:
    // Constructor.
    explicit LZSSIStream(std::istream& Src) noexcept : in(Src), bits(in) {}

    // Writes a bit to the descriptor bitfield. When the descriptor field is
    // full, it is written out.
    descriptor_t descbit() noexcept {
        return bits.pop();
    }

    // Puts a byte in the input buffer.
    uint8_t getbyte() noexcept {
        return Read1(in);
    }
};

#endif    // LIB_LZSS_HH
