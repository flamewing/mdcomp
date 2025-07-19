/*
 * Copyright (C) Flamewing 2015-2016 <flamewing.sonic@gmail.com>
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

#include "mdcomp/kosplus.hh"

#include "mdcomp/bigendian_io.hh"
#include "mdcomp/bitstream.hh"
#include "mdcomp/ignore_unused_variable_warning.hh"
#include "mdcomp/lzss.hh"
#include "mdcomp/unreachable.hh"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <istream>
#include <limits>
#include <ostream>
#include <span>
#include <sstream>
#include <type_traits>
#include <vector>

template <>
size_t moduled_kosplus::pad_mask_bits = 1U;

// NOTE: This has to be changed for other LZSS-based compression schemes.
struct kosplus_adaptor {
    enum class edge_type : uint8_t {
        invalid,
        terminator,
        symbolwise,
        dictionary_inline,
        dictionary_short,
        dictionary_long
    };

    // Flag that tells the compressor that new descriptor fields are needed
    // as soon as the last bit in the previous one is used up.
    constexpr static bool const need_early_descriptor = false;
    // Ordering of bits on descriptor field. Big bit endian order means high
    // order bits come out first.
    constexpr static bit_endian const descriptor_bit_order = bit_endian::big;
    // How many characters to skip looking for matches for at the start.
    constexpr static size_t const first_match_position = 0;
    // Size of the search buffer.
    constexpr static size_t const search_buf_size = 8192;
    // Size of the look-ahead buffer.
    constexpr static size_t const look_ahead_buf_size = 264;

    using stream_t            = uint8_t;
    using stream_endian_t     = big_endian;
    using descriptor_t        = uint8_t;
    using descriptor_endian_t = little_endian;
    using sliding_window_t    = lzss::sliding_window<kosplus_adaptor>;
    using adj_list_node       = lzss::adj_list_node<kosplus_adaptor>;
    using adj_list            = std::list<adj_list_node>;
    using istream_t           = lzss::istream<kosplus_adaptor>;
    using ostream_t           = lzss::ostream<kosplus_adaptor>;

    // Number of bits on descriptor bitfield.
    constexpr static size_t const num_desc_bits = sizeof(descriptor_t) * 8;

    // Creates the (multilayer) sliding window structure.
    static auto create_sliding_window(std::span<stream_t const> data) noexcept {
        using enum edge_type;
        return std::array{
                sliding_window_t(data, 256, 2, 5, dictionary_inline),
                sliding_window_t(data, search_buf_size, 3, 9, dictionary_short),
                sliding_window_t(
                        data, search_buf_size, 10, look_ahead_buf_size, dictionary_long)};
    }

    // Given an edge type, computes how many bits are used in the descriptor
    // field.
    constexpr static size_t desc_bits(edge_type const type) noexcept {
        // NOLINTNEXTLINE(clang-diagnostic-switch-default)
        switch (type) {
            using enum edge_type;
        case symbolwise:
            // 1-bit descriptor.
            return 1;
        case dictionary_inline:
            // 2-bit descriptor, 2-bit count.
            return 2 + 2;
        case dictionary_short:
        case dictionary_long:
        case terminator:
            // 2-bit descriptor.
            return 2;
        case invalid:
            return std::numeric_limits<size_t>::max();
        }
        utils::unreachable();
    }

    // Given an edge type, computes how many bits are used in total by this
    // edge. A return of "numeric_limits<size_t>::max()" means "infinite",
    // or "no edge".
    constexpr static size_t edge_weight(edge_type const type, size_t length) noexcept {
        ignore_unused_variable_warning(length);
        // NOLINTNEXTLINE(clang-diagnostic-switch-default)
        switch (type) {
            using enum edge_type;
        case symbolwise:
        case dictionary_inline:
            // 8-bit value/distance.
            return desc_bits(type) + 8;
        case dictionary_short:
            // 13-bit distance, 3-bit length.
            return desc_bits(type) + 13 + 3;
        case dictionary_long:
            // 13-bit distance, 3-bit marker (zero),
            // 8-bit length.
            return desc_bits(type) + 13 + 8 + 3;
        case terminator:
            // 24-bit value.
            return desc_bits(type) + 24;
        case invalid:
            return std::numeric_limits<size_t>::max();
        }
        utils::unreachable();
    }

    // KosPlus finds no additional matches over normal LZSS.
    constexpr static bool extra_matches(
            std::span<stream_t const> data, size_t const base_node, size_t const ubound,
            size_t const lbound, std::vector<adj_list_node>& matches) noexcept {
        ignore_unused_variable_warning(data, base_node, ubound, lbound, matches);
        // Do normal matches.
        return false;
    }

    // KosPlusM needs no additional padding at the end-of-file.
    constexpr static size_t get_padding(size_t const total_length) noexcept {
        ignore_unused_variable_warning(total_length);
        return 0;
    }

    constexpr static void encode_edge(ostream_t& output, adj_list_node const& edge) {
        // NOLINTNEXTLINE(clang-diagnostic-switch-default)
        switch (edge.get_type()) {
            using enum edge_type;
        case symbolwise:
            output.descriptor_bit(0b1);
            output.put_byte(edge.get_symbol());
            break;
        case dictionary_inline: {
            size_t const length = edge.get_length() - 2;
            size_t const dist   = 0x100U - edge.get_distance();
            output.descriptor_bits(0b00, 2);
            output.put_byte(dist);
            output.descriptor_bits(length & 3U, 2);
            break;
        }
        case dictionary_short: {
            size_t const length = edge.get_length();
            size_t const dist   = 0x2000U - edge.get_distance();
            size_t const high   = (dist >> 5U) & 0xF8U;
            size_t const low    = (dist & 0xFFU);
            output.descriptor_bits(0b01, 2);
            output.put_byte(high | (10 - length));
            output.put_byte(low);
            break;
        }
        case dictionary_long: {
            size_t const length = edge.get_length();
            size_t const dist   = 0x2000U - edge.get_distance();
            size_t const high   = (dist >> 5U) & 0xF8U;
            size_t const low    = (dist & 0xFFU);
            output.descriptor_bits(0b01, 2);
            output.put_byte(high);
            output.put_byte(low);
            output.put_byte(length - 9);
            break;
        }
        case terminator: {
            // Push descriptor for end-of-file marker.
            output.descriptor_bits(0b01, 2);
            // Write end-of-file marker.
            output.put_byte(0xF0);
            output.put_byte(0x00);
            output.put_byte(0x00);
            break;
        }
        case invalid:
            // This should be unreachable.
            std::cerr << std::format(
                    "Compression produced invalid edge type {}\n",
                    static_cast<size_t>(edge.get_type()));
            utils::unreachable();
        }
    }

    constexpr static bool decode_edge(
            istream_t& source, adj_list& nodes, size_t& output_size) {
        if (source.descriptor_bit() != 0U) {
            // 0b1 means symbolwise match.
            return lzss::symbolwise_match<kosplus_adaptor>(
                    nodes, output_size, source.tellg(),
                    stream_endian_t::read<stream_t>(source));
        }

        // Dictionary matches.
        if (source.descriptor_bit() == 0U) {
            // 0b00 means inline dictionary match.
            size_t const distance = 0x100U - source.get_byte();
            size_t const length   = source.descriptor_bits(2) + 2;
            return lzss::dictionary_match<kosplus_adaptor>(
                    nodes, output_size, source.tellg(), distance, length,
                    edge_type::dictionary_inline);
        }
        // 0b01 means separate dictionary match.
        uint8_t const high     = source.get_byte();
        uint8_t const low      = source.get_byte();
        size_t const  distance = 0x2000U - (((high & 0xF8U) << 5U) | low);

        if (size_t const value = high & 0x07U; value != 0U) {
            // 2-byte (short) dictionary match.
            size_t const length = 10U - value;
            return lzss::dictionary_match<kosplus_adaptor>(
                    nodes, output_size, source.tellg(), distance, length,
                    edge_type::dictionary_short);
        }
        // 3-byte (long) dictionary match.
        if (size_t const value = source.get_byte(); value != 0U) {
            // Normal long dictionary match.
            size_t const length = value + 9U;
            return lzss::dictionary_match<kosplus_adaptor>(
                    nodes, output_size, source.tellg(), distance, length,
                    edge_type::dictionary_long);
        }
        // This is the end-of-file marker.
        return lzss::terminate<kosplus_adaptor>(nodes, output_size);
    }

    constexpr static size_t output_edge(std::iostream& dest, adj_list_node const& edge) {
        using diff_t = std::make_signed_t<size_t>;
        // NOLINTNEXTLINE(clang-diagnostic-switch-default)
        switch (edge.get_type()) {
            using enum edge_type;
        case symbolwise: {
            stream_endian_t::write(dest, edge.get_symbol());
            break;
        }
        case dictionary_inline:
        case dictionary_short:
        case dictionary_long: {
            auto const distance = static_cast<diff_t>(edge.get_distance());
            auto const length   = static_cast<diff_t>(edge.get_length());
            lzss::copy<kosplus_adaptor>(dest, distance, length);
            break;
        }
        case terminator:
            break;
        case invalid:
            std::cerr << std::format(
                    "Decompression produced invalid edge type {}\n",
                    static_cast<size_t>(edge.get_type()));
            utils::unreachable();
        }
        return edge_size(edge);
    }

    constexpr static size_t edge_size(adj_list_node const& edge) {
        // NOLINTNEXTLINE(clang-diagnostic-switch-default)
        switch (edge.get_type()) {
            using enum edge_type;
        case symbolwise:
            return sizeof(stream_t);
        case dictionary_inline:
        case dictionary_short:
        case dictionary_long:
            return sizeof(stream_t) * edge.get_length();
        case terminator:
            return 0;
        case invalid:
            std::cerr << std::format(
                    "Decompression produced invalid edge type {}\n",
                    static_cast<size_t>(edge.get_type()));
            utils::unreachable();
        }
        utils::unreachable();
    }
};

static_assert(
        lzss::adaptor_t<kosplus_adaptor>,
        "kosplus_adaptor does not satisfy lzss::adaptor_t requirements");

struct kosplus_internal {
    static void decode(std::istream& input, std::iostream& dest) {
        using adaptor_t     = kosplus_adaptor;
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
    }

    static void encode(std::ostream& dest, std::span<uint8_t const> data) {
        lzss::encode(dest, data, kosplus_adaptor{});
    }
};

bool kosplus::decode(std::istream& source, std::iostream& dest) {
    auto const        location = source.tellg();
    std::stringstream input(std::ios::in | std::ios::out | std::ios::binary);
    extract(source, input);

    kosplus_internal::decode(input, dest);
    source.seekg(location + input.tellg());
    return true;
}

bool kosplus::encode(std::ostream& dest, std::span<uint8_t const> data) {
    kosplus_internal::encode(dest, data);
    return true;
}
