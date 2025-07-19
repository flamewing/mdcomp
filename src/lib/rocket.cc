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

#include "mdcomp/rocket.hh"

#include "mdcomp/bigendian_io.hh"
#include "mdcomp/bitstream.hh"
#include "mdcomp/ignore_unused_variable_warning.hh"
#include "mdcomp/lzss.hh"
#include "mdcomp/unreachable.hh"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <istream>
#include <iterator>
#include <limits>
#include <ostream>
#include <span>
#include <sstream>
#include <type_traits>
#include <vector>

template <>
size_t moduled_rocket::pad_mask_bits = 1U;

// NOTE: This has to be changed for other LZSS-based compression schemes.
struct rocket_adaptor {
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
    constexpr static bit_endian const descriptor_bit_order = bit_endian::little;
    // How many characters to skip looking for matches for at the start.
    constexpr static size_t const first_match_position = 0x3C0;
    // Size of the search buffer.
    constexpr static size_t const search_buf_size = 0x400;
    // Size of the look-ahead buffer.
    constexpr static size_t const look_ahead_buf_size = 0x40;

    using stream_t            = uint8_t;
    using stream_endian_t     = big_endian;
    using descriptor_t        = uint8_t;
    using descriptor_endian_t = little_endian;
    using sliding_window_t    = lzss::sliding_window<rocket_adaptor>;
    using adj_list_node       = lzss::adj_list_node<rocket_adaptor>;
    using adj_list            = std::list<adj_list_node>;
    using istream_t           = lzss::istream<rocket_adaptor>;
    using ostream_t           = lzss::ostream<rocket_adaptor>;

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
        // Rocket always uses a single bit descriptor.
        return type == edge_type::terminator ? 0 : 1;
    }

    // Given an edge type, computes how many bits are used in total by this
    // edge. A return of "numeric_limits<size_t>::max()" means "infinite",
    // or "no edge".
    constexpr static size_t edge_weight(edge_type const type, size_t length) noexcept {
        ignore_unused_variable_warning(length);
        // NOLINTNEXTLINE(clang-diagnostic-switch-default)
        switch (type) {
            using enum edge_type;
        case terminator:
            // Does not have a terminator.
            return 0;
        case symbolwise:
            // 8-bit value.
            return desc_bits(type) + 8;
        case dictionary:
            // 10-bit distance, 6-bit length.
            return desc_bits(type) + 10 + 6;
        case invalid:
            return std::numeric_limits<size_t>::max();
        }
        utils::unreachable();
    }

    // Rocket finds no additional matches over normal LZSS.
    static bool extra_matches(
            std::span<stream_t const> data, size_t const base_node, size_t const ubound,
            size_t const lbound, std::vector<adj_list_node>& matches) noexcept {
        ignore_unused_variable_warning(data, base_node, ubound, lbound, matches);
        // Do normal matches.
        return false;
    }

    // Rocket needs no additional padding at the end-of-file.
    constexpr static size_t get_padding(size_t const total_length) noexcept {
        ignore_unused_variable_warning(total_length);
        return 0;
    }

    constexpr static void encode_edge(ostream_t& output, adj_list_node const& edge) {
        // NOLINTNEXTLINE(clang-diagnostic-switch-default)
        switch (edge.get_type()) {
            using enum edge_type;
        case symbolwise:
            output.descriptor_bit(1);
            output.put_byte(edge.get_symbol());
            break;
        case dictionary: {
            size_t const length = edge.get_length();
            size_t const dist   = edge.get_distance();
            size_t const position
                    = (edge.get_position() - dist) % rocket_adaptor::search_buf_size;
            output.descriptor_bit(0);
            output.put_byte(((length - 1) << 2U) | (position >> 8U));
            output.put_byte(position);
            break;
        }
        case terminator:
            break;
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
        if (source.eof() || source.peek_eof()) {
            // If we reach the end of the stream, we should not decode any more edges.
            return lzss::terminate<rocket_adaptor>(nodes, output_size);
        }
        if (source.descriptor_bit() != 0U) {
            // Symbolwise match.
            return lzss::symbolwise_match<rocket_adaptor>(
                    nodes, output_size, source.tellg(),
                    stream_endian_t::read<stream_t>(source));
        }
        // Dictionary match.
        // Distance and length of match.
        constexpr size_t const bias        = rocket_adaptor::first_match_position;
        constexpr size_t const buffer_size = rocket_adaptor::search_buf_size;

        size_t const high   = source.get_byte();
        size_t const low    = source.get_byte();
        size_t const base   = output_size;
        size_t const length = ((high & 0xFCU) >> 2U) + 1U;
        size_t const offset = ((high & 3U) << 8U) | low;
        // The offset is stored as being absolute within a 0x400-byte
        // buffer, starting at position 0x3C0. We just rebase it around
        // base + 0x3C0u.
        size_t const distance = buffer_size - ((offset - base - bias) % buffer_size);
        return lzss::dictionary_match<rocket_adaptor>(
                nodes, output_size, source.tellg(), distance, length,
                edge_type::dictionary);
    }

    constexpr static size_t output_edge(std::iostream& dest, adj_list_node const& edge) {
        using diff_t = std::make_signed_t<size_t>;
        // NOLINTNEXTLINE(clang-diagnostic-switch-default)
        switch (edge.get_type()) {
            using enum edge_type;
        case symbolwise:
            stream_endian_t::write(dest, edge.get_symbol());
            break;
        case dictionary: {
            auto       distance = static_cast<diff_t>(edge.get_distance());
            auto       length   = static_cast<diff_t>(edge.get_length());
            auto const base     = static_cast<diff_t>(dest.tellp());
            auto const delta    = distance - base;
            if (delta > 0) {
                diff_t const count = std::min(length, delta);
                std::ranges::fill_n(std::ostreambuf_iterator<char>(dest), count, 0x20);
                length -= count;
                distance -= count;
            }
            if (length > 0) {
                lzss::copy<rocket_adaptor>(dest, distance, length);
            }
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
        case dictionary:
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
        lzss::adaptor_t<rocket_adaptor>,
        "rocket_adaptor does not satisfy lzss::adaptor_t requirements");

struct rocket_internal {
    static void decode(std::istream& input, std::iostream& dest) {
        auto const uncompressed_size = static_cast<size_t>(big_endian::read2(input));
        auto const compressed_size
                = static_cast<std::streamsize>(big_endian::read2(input)) + 4;

        using adaptor_t     = rocket_adaptor;
        using stream_t      = lzss::istream<adaptor_t>;
        using adj_list_node = adaptor_t::adj_list_node;
        using adj_list      = std::list<adj_list_node>;

        adj_list                list;
        [[maybe_unused]] size_t output_size = 0;
        {
            stream_t source(input);
            while (input.good() && input.tellg() < compressed_size
                   && output_size < uncompressed_size
                   && adaptor_t::decode_edge(source, list, output_size)) {
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

    static void encode(std::ostream& dest, std::span<uint8_t const> data) {
        lzss::encode(dest, data, rocket_adaptor{});
    }
};

bool rocket::decode(std::istream& source, std::iostream& dest) {
    auto const        location = source.tellg();
    std::stringstream input(std::ios::in | std::ios::out | std::ios::binary);
    extract(source, input);

    rocket_internal::decode(input, dest);
    source.seekg(location + input.tellg());
    return true;
}

bool rocket::encode(std::istream& source, std::ostream& dest) {
    // We will pre-fill the buffer with 0x3C0 0x20's.
    std::stringstream input(std::ios::in | std::ios::out | std::ios::binary);
    std::ranges::fill_n(
            std::ostreambuf_iterator<char>(input), rocket_adaptor::first_match_position,
            0x20);
    // Copy to buffer.
    input << source.rdbuf();
    input.seekg(0);
    return basic_rocket::encode(input, dest);
}

bool rocket::encode(std::ostream& dest, std::span<uint8_t const> data) {
    // Internal buffer.
    std::stringstream out_buff(std::ios::in | std::ios::out | std::ios::binary);
    rocket_internal::encode(out_buff, data);

    // Fill in header
    // Size of decompressed file
    big_endian::write2(
            dest,
            static_cast<uint16_t>(data.size() - rocket_adaptor::first_match_position));
    // Size of compressed file
    big_endian::write2(dest, static_cast<uint16_t>(out_buff.tellp()));

    out_buff.seekg(0);
    dest << out_buff.rdbuf();
    return true;
}
