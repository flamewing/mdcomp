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

#include "mdcomp/bigendian_io.hh"
#include "mdcomp/bitstream.hh"

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
    size_t position{0};
    // Cost, in bits, of "covering" all of the characters in the match.
    EdgeType type;

    union {
        MatchInfo match;
        stream_t  symbol;
    };

public:
    // Constructors.
    constexpr AdjListNode() noexcept : type(EdgeType::invalid), symbol(stream_t(0)) {}

    constexpr AdjListNode(size_t position_, stream_t symbol, EdgeType type_) noexcept
            : position(position_), type(type_), symbol(symbol) {}

    constexpr AdjListNode(size_t position_, MatchInfo match_, EdgeType type_) noexcept
            : position(position_), type(type_), match(match_) {}

    // Getters.
    [[nodiscard]] constexpr size_t get_position() const noexcept {
        return position;
    }

    [[nodiscard]] constexpr size_t get_destination() const noexcept {
        return position + get_length();
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
            Data_t data_, size_t const search_buffer_size_,
            size_t const minimal_match_length_, size_t const lookahead_buffer_length,
            EdgeType const type_) noexcept
            : data(data_), search_buffer_size(search_buffer_size_),
              minimal_match_length(minimal_match_length_),
              base_node(Adaptor::FirstMatchPosition),
              upper_bound(std::min(lookahead_buffer_length + base_node, data.size())),
              lower_bound(
                      base_node > search_buffer_size ? base_node - search_buffer_size
                                                     : 0),
              type(type_) {}

    [[nodiscard]] size_t get_data_size() const {
        return data.size();
    }

    [[nodiscard]] size_t get_search_buffer_size() const {
        return base_node - lower_bound;
    }

    [[nodiscard]] size_t get_lookahead_buffer_size() const {
        return upper_bound - base_node;
    }

    [[nodiscard]] size_t get_window_size() const {
        return upper_bound - lower_bound;
    }

    bool slide_window() noexcept {
        if (upper_bound != data.size()) {
            upper_bound++;
        }
        if (base_node != data.size()) {
            base_node++;
        }
        if (get_search_buffer_size() > search_buffer_size) {
            lower_bound++;
        }
        return get_lookahead_buffer_size() != 0;
    }

    void find_matches(MatchVector& matches) const noexcept {
        static_assert(
                noexcept(Adaptor::edge_weight(EdgeType(), size_t())),
                "Adaptor::edge_weight() is not noexcept");
        size_t const end = get_lookahead_buffer_size();
        // This is what we produce.
        matches.clear();
        // First node is special.
        if (get_search_buffer_size() == 0) {
            return;
        }
        size_t base     = base_node - 1;
        size_t best_pos = 0;
        size_t best_len = 0;
        do {
            // Keep looking for dictionary matches.
            size_t offset = 0;
            while (offset < end && data[base + offset] == data[base_node + offset]) {
                ++offset;
            }
            if (best_len < offset) {
                best_pos = base;
                best_len = offset;
            }
            if (offset == end) {
                break;
            }
        } while (base-- > lower_bound);

        if (best_len >= minimal_match_length) {
            // We have found a match that links (base_node) with
            // (base_node + best_len) with length (best_len) and distance
            // equal to (base_node-best_pos).
            // Add it, and all prefixes, to the list, as long as it is a better
            // match.
            for (size_t jj = minimal_match_length; jj <= best_len; ++jj) {
                matches.emplace_back(base_node, Match_t{base_node - best_pos, jj}, type);
            }
        }
    }

    bool find_extra_matches(MatchVector& matches) const noexcept {
        static_assert(
                noexcept(Adaptor::extra_matches(
                        data, base_node, upper_bound, lower_bound,
                        std::declval<MatchVector&>())),
                "Adaptor::extra_matches() is not noexcept");
        // This is what we produce.
        matches.clear();
        // Get extra dictionary matches dependent on specific encoder.
        return Adaptor::extra_matches(data, base_node, upper_bound, lower_bound, matches);
    }

private:
    // Source file data and its size; one node per character in source file.
    Data_t         data;
    size_t const   search_buffer_size;
    size_t const   minimal_match_length;
    size_t         base_node;
    size_t         upper_bound;
    size_t         lower_bound;
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

    auto read_stream = [](uint8_t const*& pointer) {
        return stream_endian_t::template Read<stream_t>(pointer);
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
    std::vector<size_t> descriptor_costs(
            numNodes + 1, std::numeric_limits<size_t>::max());
    descriptor_costs[0] = 0;

    // Extracting distance relax logic from the loop so it can be used more
    // often.
    auto Relax = [last_node = nlen, &costs, &descriptor_costs, &parents, &pedges](
                         size_t index, size_t const base_descriptor_cost,
                         auto const& elem) {
        // Need destination ID and edge weight.
        size_t const next_node   = elem.get_destination() - Adaptor::FirstMatchPosition;
        size_t       edge_weight = costs[index] + elem.get_weight();
        // Compute descriptor bits from using this edge.
        size_t descriptor_cost
                = base_descriptor_cost + Adaptor::desc_bits(elem.get_type());
        if (next_node == last_node) {
            // This is the ending node. Add the descriptor bits for the
            // end-of-file marker.
            edge_weight += Adaptor::edge_weight(EdgeType::terminator, 0);
            descriptor_cost += Adaptor::desc_bits(EdgeType::terminator);
            // If the descriptor bitfield had exactly 0 bits left after
            // this, we may need to emit a new descriptor bitfield (the
            // full Adaptor::NumDescBits bits). Otherwise, we need to
            // pads the last descriptor bitfield to full size.
            // This accomplishes both.
            size_t const descriptor_modulus = descriptor_cost % Adaptor::NumDescBits;
            if (descriptor_modulus != 0 || Adaptor::NeedEarlyDescriptor) {
                edge_weight += (Adaptor::NumDescBits - descriptor_modulus);
                descriptor_cost += (Adaptor::NumDescBits - descriptor_modulus);
            }
            // Compensate for the Adaptor's padding, if any.
            edge_weight += Adaptor::get_padding(edge_weight);
        }
        // Is the cost to reach the target node through this edge less
        // than the current cost?
        if (costs[next_node] > edge_weight) {
            // If so, update the data structures with new best edge.
            costs[next_node]            = edge_weight;
            parents[next_node]          = index;
            pedges[next_node]           = elem;
            descriptor_costs[next_node] = descriptor_cost;
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
        size_t const base_descriptor_cost = descriptor_costs[ii];
        // Start with the literal/symbolwise encoding of the current node.
        {
            size_t const   offset  = ii + Adaptor::FirstMatchPosition;
            auto const*    pointer = reinterpret_cast<uint8_t const*>(data + offset);
            stream_t const value   = read_stream(pointer);
            EdgeType const type    = EdgeType::symbolwise;
            Relax(ii, base_descriptor_cost, Node_t(offset, value, type));
        }
        // Get the adjacency list for this node.
        for (auto& window : winSet) {
            if (!window.find_extra_matches(matches)) {
                window.find_matches(matches);
            }
            for (auto const& elem : matches) {
                if (elem.get_type() != EdgeType::invalid) {
                    Relax(ii, base_descriptor_cost, elem);
                }
            }
            window.slide_window();
        }
    }

    // This is what we will produce.
    lzss_parse_result<AdjList> result{
            {Node_t{0, 0, EdgeType::terminator}}, descriptor_costs.back(), costs.back()};
    AdjList& parse_list = result.parse_list;
    for (size_t ii = numNodes; ii != 0;) {
        // Insert the edge up front...
        parse_list.push_front(pedges[ii]);
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
    using bit_buffer_t
            = obitstream<descriptor_t, Adaptor::DescriptorBitOrder, descriptor_endian_t>;
    // Where we will output to.
    std::ostream& out;
    // Internal bitstream output buffer.
    bit_buffer_t bits;
    // Internal parameter buffer.
    std::string buffer;

    void flush_buffer() noexcept {
        out.write(buffer.c_str(), buffer.size());
        buffer.clear();
    }

public:
    // Constructor.
    explicit LZSSOStream(std::ostream& Dest) noexcept : out(Dest), bits(out) {}

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
        bool const need_dummy_descriptor = !bits.have_waiting_bits();
        // Now, flush the queue if needed.
        bits.flush();
        if constexpr (Adaptor::NeedEarlyDescriptor) {
            if (need_dummy_descriptor) {
                // We need to add a dummy descriptor field; so add it.
                descriptor_endian_t::Write(out, descriptor_t{0});
            }
        }
        // Now write the terminating sequence if it wasn't written already.
        flush_buffer();
    }

    // Writes a bit to the descriptor bitfield. When the descriptor field is
    // full, outputs it and the output parameter buffer.
    void descriptor_bit(descriptor_t const bit) noexcept {
        if constexpr (Adaptor::NeedEarlyDescriptor) {
            if (bits.push(bit)) {
                flush_buffer();
            }
        } else {
            if (!bits.have_waiting_bits()) {
                flush_buffer();
            }
            bits.push(bit);
        }
    }

    // Puts a byte in the output buffer.
    void put_byte(size_t const value) noexcept {
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
    using bit_buffer_t        = ibitstream<
            descriptor_t, Adaptor::DescriptorBitOrder, descriptor_endian_t,
            Adaptor::NeedEarlyDescriptor>;
    // Where we will input to.
    std::istream& in;
    // Internal bitstream input buffer.
    bit_buffer_t bits;

public:
    // Constructor.
    explicit LZSSIStream(std::istream& Source) noexcept : in(Source), bits(in) {}

    // Writes a bit to the descriptor bitfield. When the descriptor field is
    // full, it is written out.
    descriptor_t descriptor_bit() noexcept {
        return bits.pop();
    }

    // Puts a byte in the input buffer.
    uint8_t get_byte() noexcept {
        return Read1(in);
    }
};

#endif    // LIB_LZSS_HH
