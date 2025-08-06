/*
 * Copyright (C) Flamewing 2013-2025 <flamewing.sonic@gmail.com>
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

#include "mdcomp/comperx.hh"

#include "mdcomp/bigendian_io.hh"
#include "mdcomp/bitstream.hh"
#include "mdcomp/ignore_unused_variable_warning.hh"
#include "mdcomp/lzss.hh"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <istream>
#include <limits>
#include <list>
#include <ostream>
#include <span>
#include <sstream>
#include <type_traits>
#include <vector>

template <>
size_t moduled_comperx::pad_mask_bits = 1U;

// NOTE: This has to be changed for other LZSS-based compression schemes.
struct comperx_adaptor {
    enum class edge_type : uint8_t {
        invalid,
        terminator,
        symbolwise,
        dictionary
    };

    // Flag that tells the compressor that new descriptor fields is needed
    // when a new bit is needed and all bits in the previous one have been
    // used up.
    constexpr static bool const need_early_descriptor = false;
    // Ordering of bits on descriptor field. Big bit endian order means high
    // order bits come out first.
    constexpr static bit_endian const descriptor_bit_order = bit_endian::big;
    // How many characters to skip looking for matches for at the start.
    constexpr static size_t const first_match_position = 0;
    // Size of the search buffer.
    constexpr static size_t const search_buf_size = 256;
    // Size of the look-ahead buffer.
    constexpr static size_t const look_ahead_buf_size = 255;

    using stream_t            = uint16_t;
    using stream_endian_t     = big_endian;
    using descriptor_t        = uint16_t;
    using descriptor_endian_t = big_endian;
    using sliding_window_t    = lzss::sliding_window<comperx_adaptor>;
    using adj_list_node       = lzss::adj_list_node<comperx_adaptor>;
    using adj_list            = std::list<adj_list_node>;
    using istream_t           = lzss::istream<comperx_adaptor>;
    using ostream_t           = lzss::ostream<comperx_adaptor>;

    // Number of bits on descriptor bitfield.
    constexpr static size_t const num_desc_bits = sizeof(descriptor_t) * 8;

    // Creates the (multilayer) sliding window structure.
    static auto create_sliding_window(std::span<stream_t const> data) noexcept {
        return std::array{sliding_window_t(
                data, search_buf_size, 2, look_ahead_buf_size, edge_type::dictionary)};
    }

    // Given an edge type, computes how many bits are used in the descriptor
    // field.
    constexpr static size_t desc_bits(edge_type const type) noexcept {
        // Comper always uses a single bit descriptor.
        ignore_unused_variable_warning(type);
        return 1;
    }

    // Given an edge type, computes how many bits are used in total by this
    // edge. A return of "numeric_limits<size_t>::max()" means "infinite",
    // or "no edge".
    constexpr static size_t edge_weight(edge_type const type, size_t length) noexcept {
        ignore_unused_variable_warning(length);
        switch (type) {
            using enum edge_type;
        case symbolwise:
        case terminator:
            // 16-bit value.
            return desc_bits(type) + 16;
        case dictionary:
            // 8-bit distance, 8-bit length.
            return desc_bits(type) + 8 + 8;
        // NOLINTNEXTLINE(clang-diagnostic-covered-switch-default)
        default:
        case invalid:
            return std::numeric_limits<size_t>::max();
        }
    }

    // ComperX finds no additional matches over normal LZSS.
    constexpr static bool extra_matches(
            std::span<stream_t const> data, size_t const base_node, size_t const ubound,
            size_t const lbound, std::vector<adj_list_node>& matches) noexcept {
        ignore_unused_variable_warning(data, base_node, ubound, lbound, matches);
        // Do normal matches.
        return false;
    }

    // ComperX needs no additional padding at the end-of-file.
    constexpr static size_t get_padding(size_t const total_length) noexcept {
        ignore_unused_variable_warning(total_length);
        return 0;
    }

    constexpr static void encode_edge(ostream_t& output, adj_list_node const& edge) {
        switch (edge.get_type()) {
            using enum edge_type;
        case symbolwise: {
            size_t const value = edge.get_symbol();
            size_t const high  = (value >> 8U) & 0xFFU;
            size_t const low   = (value & 0xFFU);
            output.descriptor_bit(0);
            output.put_byte(high);
            output.put_byte(low);
            break;
        }
        case dictionary: {
            size_t const distance = edge.get_distance();
            size_t const length   = 0x101U - edge.get_length();
            output.descriptor_bit(1);
            output.put_byte(distance);
            output.put_byte(std::rotr(static_cast<uint8_t>(length - 2U), 1) ^ 0x7FU);
            break;
        }
        case terminator: {
            // Push descriptor for end-of-file marker.
            output.descriptor_bit(1);
            output.put_byte(0xffU);
            output.put_byte(0);
            break;
        }
        // NOLINTNEXTLINE(clang-diagnostic-covered-switch-default)
        default:
        case invalid:
            std::cerr << "Compression produced invalid edge type "
                      << static_cast<size_t>(edge.get_type()) << '\n';
            break;
        }
    }

    constexpr static bool decode_edge(
            istream_t& source, adj_list& nodes, size_t& output_size) {
        if (source.descriptor_bit() == 0U) {
            // Symbolwise match.
            return lzss::symbolwise_match<comperx_adaptor>(
                    nodes, output_size, source.tellg(),
                    stream_endian_t::read<stream_t>(source));
        }

        // Dictionary match.
        // Distance and length of match.
        size_t const distance = 0x101U - source.get_byte();

        if (size_t const value = source.get_byte(); value != 0) {
            size_t const length = std::rotl(static_cast<uint8_t>(value ^ 0x7FU), 1) + 2U;
            return lzss::dictionary_match<comperx_adaptor>(
                    nodes, output_size, source.tellg(), distance, length,
                    edge_type::dictionary);
        }
        return lzss::terminate<comperx_adaptor>(nodes, output_size);
    }

    constexpr static size_t output_edge(std::iostream& dest, adj_list_node const& edge) {
        using diff_t = std::make_signed_t<size_t>;
        switch (edge.get_type()) {
            using enum edge_type;
        case symbolwise: {
            stream_endian_t::write(dest, edge.get_symbol());
            break;
        }
        case dictionary: {
            auto const distance = static_cast<diff_t>(edge.get_distance());
            auto const length   = static_cast<diff_t>(edge.get_length());
            lzss::copy<comperx_adaptor>(dest, distance, length);
            break;
        }
        case terminator:
            break;
        // NOLINTNEXTLINE(clang-diagnostic-covered-switch-default)
        default:
        case invalid:
            std::cerr << "Decompression produced invalid edge type "
                      << static_cast<size_t>(edge.get_type()) << '\n';
            break;
        }
        return edge_size(edge);
    }

    constexpr static size_t edge_size(adj_list_node const& edge) {
        switch (edge.get_type()) {
            using enum edge_type;
        case symbolwise:
            return sizeof(stream_t);
        case dictionary:
            return sizeof(stream_t) * edge.get_length();
        case terminator:
            return 0;
        // NOLINTNEXTLINE(clang-diagnostic-covered-switch-default)
        default:
        case invalid:
            std::cerr << "Decompression produced invalid edge type "
                      << static_cast<size_t>(edge.get_type()) << '\n';
            return 0;
        }
    }
};

static_assert(
        lzss::adaptor_t<comperx_adaptor>,
        "comperx_adaptor does not satisfy lzss::adaptor_t requirements");

struct comperx_internal {
    static void decode(std::istream& input, std::iostream& dest) {
        using adaptor_t     = comperx_adaptor;
        using stream_t      = lzss::istream<adaptor_t>;
        using adj_list_node = adaptor_t::adj_list_node;
        using adj_list      = std::list<adj_list_node>;

        adj_list                list;
        [[maybe_unused]] size_t output_size = 0;
        {
            stream_t source(input);
            while (input.good() && adaptor_t::decode_edge(source, list, output_size)) {
                // Continue decoding until we reach the end of the stream.
            }
        }

        for (auto const& edge : list) {
            adaptor_t::output_edge(dest, edge);
        }
    }

    static void encode(std::ostream& dest, std::span<char const> data) {
        lzss::encode(dest, data, comperx_adaptor{});
    }
};

bool comperx::decode(std::istream& source, std::iostream& dest) {
    auto const        location = source.tellg();
    std::stringstream input(std::ios::in | std::ios::out | std::ios::binary);
    extract(source, input);

    comperx_internal::decode(input, dest);
    source.seekg(location + input.tellg());
    return true;
}

bool comperx::encode(std::ostream& dest, std::span<char const> data) {
    comperx_internal::encode(dest, data);
    return true;
}
