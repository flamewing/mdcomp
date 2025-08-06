/*
 * Copyright (C) Flamewing 2011-2025 <flamewing.sonic@gmail.com>
 * Copyright (C) 2002-2004 The KENS Project Development Team
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

#include "mdcomp/lzkn1.hh"

#include "mdcomp/bigendian_io.hh"
#include "mdcomp/bitstream.hh"
#include "mdcomp/ignore_unused_variable_warning.hh"
#include "mdcomp/lzss.hh"
#include "mdcomp/stream_utils.hh"

#include <algorithm>
#include <array>
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
size_t moduled_lzkn1::pad_mask_bits = 1U;

// NOTE: This has to be changed for other LZSS-based compression schemes.
struct lzkn1_adaptor {
    enum class edge_type : uint8_t {
        invalid,
        terminator,
        symbolwise,
        dictionary_short,
        dictionary_long,
        packed_symbolwise
    };

    // Flag that tells the compressor that new descriptor fields are needed
    // as soon as the last bit in the previous one is used up.
    constexpr static bool const need_early_descriptor = false;
    // Ordering of bits on descriptor field. Big bit endian order means high
    // order bits come out first.
    constexpr static bit_endian const descriptor_bit_order = bit_endian::little;
    // How many characters to skip looking for matches for at the start.
    constexpr static size_t const first_match_position = 0;
    // Size of the search buffer.
    constexpr static size_t const search_buf_size = 1023;
    // Size of the look-ahead buffer.
    constexpr static size_t const look_ahead_buf_size = 33;

    using stream_t            = uint8_t;
    using stream_endian_t     = big_endian;
    using descriptor_t        = uint8_t;
    using descriptor_endian_t = big_endian;
    using sliding_window_t    = lzss::sliding_window<lzkn1_adaptor>;
    using adj_list_node       = lzss::adj_list_node<lzkn1_adaptor>;
    using adj_list            = std::list<adj_list_node>;
    using istream_t           = lzss::istream<lzkn1_adaptor>;
    using ostream_t           = lzss::ostream<lzkn1_adaptor>;

    // Number of bits on descriptor bitfield.
    constexpr static size_t const num_desc_bits = sizeof(descriptor_t) * 8;

    // Creates the (multilayer) sliding window structure.
    static auto create_sliding_window(std::span<stream_t const> data) noexcept {
        return std::array{
                sliding_window_t(data, 15, 2, 5, edge_type::dictionary_short),
                sliding_window_t(
                        data, search_buf_size, 3, look_ahead_buf_size,
                        edge_type::dictionary_long)};
    }

    // Given an edge type, computes how many bits are used in the descriptor
    // field.
    constexpr static size_t desc_bits(edge_type const type) noexcept {
        ignore_unused_variable_warning(type);
        return 1;
    }

    // Given an edge type, computes how many bits are used in total by this
    // edge. A return of "numeric_limits<size_t>::max()" means "infinite",
    // or "no edge".
    constexpr static size_t edge_weight(edge_type const type, size_t length) noexcept {
        switch (type) {
            using enum edge_type;
        case symbolwise:
        case terminator:
            // 8-bit value.
            return desc_bits(type) + 8;
        case dictionary_short:
            // 4-bit distance, 2-bit marker (%10),
            // 2-bit length.
            return desc_bits(type) + 4 + 2 + 2;
        case dictionary_long:
            // 10-bit distance, 1-bit marker (%0),
            // 5-bit length.
            return desc_bits(type) + 10 + 1 + 5;
        case packed_symbolwise:
            // 2-bit marker (%11), 6-bit length,
            // length * 8 bits data.
            return desc_bits(type) + 2 + 6 + (length * 8);
        // NOLINTNEXTLINE(clang-diagnostic-covered-switch-default)
        default:
        case invalid:
            return std::numeric_limits<size_t>::max();
        }
    }

    // lzkn1 finds no additional matches over normal LZSS.
    static bool extra_matches(
            std::span<stream_t const> data, size_t const base_node, size_t const ubound,
            size_t const lbound, std::vector<adj_list_node>& matches) noexcept {
        ignore_unused_variable_warning(data, lbound);
        // Add packed symbolwise matches.
        size_t const end = std::min(ubound - base_node, size_t{72});
        for (size_t ii = 8; ii < end; ii++) {
            lzss::symbolwise_info<lzkn1_adaptor>(
                    matches, static_cast<std::make_signed_t<size_t>>(base_node),
                    data.data(), ii, edge_type::packed_symbolwise);
        }
        // Do normal matches.
        return false;
    }

    // lzkn1M needs to pad each module to a multiple of 16 bytes.
    static size_t get_padding(size_t const total_length) noexcept {
        ignore_unused_variable_warning(total_length);
        return 0;
    }

    constexpr static void encode_edge(ostream_t& output, adj_list_node const& edge) {
        constexpr uint8_t const eof_marker               = 0x1FU;
        constexpr uint8_t const packed_symbolwise_marker = 0xC0U;
        constexpr uint8_t const max_byte = std::numeric_limits<uint8_t>::max();
        switch (edge.get_type()) {
            using enum edge_type;
        case symbolwise:
            output.descriptor_bit(0);
            output.put_byte(edge.get_symbol());
            break;
        case packed_symbolwise: {
            output.descriptor_bit(1);
            auto const    data  = edge.get_data();
            size_t const  count = data.size();
            uint8_t const value = (count + packed_symbolwise_marker - 8U) & max_byte;
            output.put_byte(value);
            for (auto const& elem : data) {
                output.put_byte(elem);
            }
            break;
        }
        case dictionary_short: {
            output.descriptor_bit(1);
            size_t const  count    = edge.get_length();
            size_t const  distance = edge.get_distance();
            uint8_t const value    = (((count + 6U) << 4U) | distance) & max_byte;
            output.put_byte(value);
            break;
        }
        case dictionary_long: {
            output.descriptor_bit(1);
            size_t const  count    = edge.get_length();
            size_t const  distance = edge.get_distance();
            uint8_t const high = ((count - 3U) | ((distance & 0x300U) >> 3U)) & max_byte;
            uint8_t const low  = distance & 0xFFU;
            output.put_byte(high);
            output.put_byte(low);
            break;
        }
        case terminator: {
            // Push descriptor for end-of-file marker.
            output.descriptor_bit(1);
            // Write end-of-file marker.
            output.put_byte(eof_marker);
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
        constexpr uint8_t const eof_marker               = 0x1FU;
        constexpr uint8_t const packed_symbolwise_marker = 0xC0U;
        constexpr uint8_t const short_match_marker       = 0x80U;

        if (source.descriptor_bit() == 0U) {
            // Symbolwise match.
            return lzss::symbolwise_match<lzkn1_adaptor>(
                    nodes, output_size, source.tellg(),
                    stream_endian_t::read<stream_t>(source));
        }
        // Dictionary matches or packed symbolwise match.
        uint8_t const value = source.get_byte();
        if (value == eof_marker) {
            // Terminator.
            return lzss::terminate<lzkn1_adaptor>(nodes, output_size);
        }

        if ((value & packed_symbolwise_marker) == packed_symbolwise_marker) {
            // Packed symbolwise.
            size_t const length = value - packed_symbolwise_marker + 8U;
            // TODO: we are *not* using the `adj_list_node::symbolwise_info`
            // for the time being because we have an input stream instead of
            // a span of bytes. When we further refactor the encoder/decoder
            // to always work with spans for input, this will change.
            return lzss::symbolwise_data<lzkn1_adaptor>(
                    nodes, output_size, source.tellg(), source, length,
                    edge_type::packed_symbolwise);
        }

        // Dictionary matches.
        if ((value & short_match_marker) == short_match_marker) {
            // Short dictionary match.
            size_t const distance = value & 0xFU;
            size_t const length   = (value >> 4U) - 6U;
            return lzss::dictionary_match<lzkn1_adaptor>(
                    nodes, output_size, source.tellg(), distance, length,
                    edge_type::dictionary_short);
        }
        // Long dictionary match.
        uint8_t const high     = value;
        uint8_t const low      = source.get_byte();
        size_t const  distance = ((high * 8U) & 0x300U) | low;
        size_t const  length   = (high & 0x1FU) + 3U;
        return lzss::dictionary_match<lzkn1_adaptor>(
                nodes, output_size, source.tellg(), distance, length,
                edge_type::dictionary_long);
    }

    constexpr static size_t output_edge(std::iostream& dest, adj_list_node const& edge) {
        using diff_t = std::make_signed_t<size_t>;
        switch (edge.get_type()) {
            using enum edge_type;
        case symbolwise: {
            stream_endian_t::write(dest, edge.get_symbol());
            break;
        }
        case packed_symbolwise: {
            auto const data = edge.get_data();
            detail::write_as_bytes(dest, data);
            break;
        }
        case dictionary_short:
        case dictionary_long: {
            auto const distance = static_cast<diff_t>(edge.get_distance());
            auto const length   = static_cast<diff_t>(edge.get_length());
            lzss::copy<lzkn1_adaptor>(dest, distance, length);
            break;
        }
        case terminator: {
            break;
        }
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
        // NOLINTNEXTLINE(bugprone-branch-clone)
        case packed_symbolwise:
        case dictionary_short:
        case dictionary_long:
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
        lzss::adaptor_t<lzkn1_adaptor>,
        "lzkn1_adaptor does not satisfy lzss::adaptor_t requirements");

struct lzkn1_internal {
    static void decode(std::istream& input, std::iostream& dest) {
        size_t const uncompressed_size = big_endian::read2(input);

        using adaptor_t     = lzkn1_adaptor;
        using stream_t      = lzss::istream<adaptor_t>;
        using adj_list_node = adaptor_t::adj_list_node;
        using adj_list      = std::list<adj_list_node>;

        adj_list                list;
        [[maybe_unused]] size_t output_size = 0;
        {
            stream_t source(input);
            while (input.good() && adaptor_t::decode_edge(source, list, output_size)) {
                // Continue decoding until we reach the end of the input.
            }
        }

        for (auto const& edge : list) {
            adaptor_t::output_edge(dest, edge);
        }

        if (output_size != uncompressed_size) {
            std::cerr << "Something went wrong; expected " << uncompressed_size
                      << " bytes, got " << output_size << " bytes instead.\n";
        }
    }

    static void encode(std::ostream& dest, std::span<char const> data) {
        big_endian::write2(dest, data.size() & std::numeric_limits<uint16_t>::max());
        lzss::encode(dest, data, lzkn1_adaptor{});
    }
};

bool lzkn1::decode(std::istream& source, std::iostream& dest) {
    auto const        location = source.tellg();
    std::stringstream input(std::ios::in | std::ios::out | std::ios::binary);
    extract(source, input);

    lzkn1_internal::decode(input, dest);
    source.seekg(location + input.tellg());
    return true;
}

bool lzkn1::encode(std::ostream& dest, std::span<char const> data) {
    lzkn1_internal::encode(dest, data);
    return true;
}
