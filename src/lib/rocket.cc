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

#include <cstdint>
#include <iostream>
#include <istream>
#include <ostream>
#include <sstream>
#include <type_traits>

using std::array;
using std::ios;
using std::iostream;
using std::istream;
using std::numeric_limits;
using std::ostream;
using std::ostreambuf_iterator;
using std::stringstream;

template <>
size_t moduled_rocket::pad_mask_bits = 1U;

struct rocket_internal {
    // NOTE: This has to be changed for other LZSS-based compression schemes.
    struct rocket_adaptor {
        using stream_t            = uint8_t;
        using stream_endian_t     = big_endian;
        using descriptor_t        = uint8_t;
        using descriptor_endian_t = little_endian;
        using sliding_window_t    = sliding_window<rocket_adaptor>;
        enum class edge_type : size_t {
            invalid,
            terminator,
            symbolwise,
            dictionary
        };
        // Number of bits on descriptor bitfield.
        constexpr static size_t const num_desc_bits = sizeof(descriptor_t) * 8;
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

        // Creates the (multilayer) sliding window structure.
        static auto create_sliding_window(std::span<stream_t const> data) noexcept {
            return array{
                    sliding_window_t{
                                     data, search_buf_size, 2, look_ahead_buf_size,
                                     edge_type::dictionary}
            };
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
        constexpr static size_t edge_weight(
                edge_type const type, size_t length) noexcept {
            ignore_unused_variable_warning(length);
            switch (type) {
            case edge_type::terminator:
                // Does not have a terminator.
                return 0;
            case edge_type::symbolwise:
                // 8-bit value.
                return desc_bits(type) + 8;
            case edge_type::dictionary:
                // 10-bit distance, 6-bit length.
                return desc_bits(type) + 10 + 6;
            case edge_type::invalid:
                return numeric_limits<size_t>::max();
            }
            __builtin_unreachable();
        }

        // Rocket finds no additional matches over normal LZSS.
        static bool extra_matches(
                std::span<stream_t const> data, size_t const base_node,
                size_t const ubound, size_t const lbound,
                std::vector<adj_list_node<rocket_adaptor>>& matches) noexcept {
            ignore_unused_variable_warning(data, base_node, ubound, lbound, matches);
            // Do normal matches.
            return false;
        }

        // Rocket needs no additional padding at the end-of-file.
        constexpr static size_t get_padding(size_t const total_length) noexcept {
            ignore_unused_variable_warning(total_length);
            return 0;
        }
    };

public:
    static void decode(istream& input, iostream& dest) {
        using rock_istream = lzss_istream<rocket_adaptor>;
        using diff_t       = std::make_signed_t<size_t>;

        input.ignore(2);
        auto const   size = static_cast<std::streamsize>(big_endian::read2(input)) + 4;
        rock_istream source(input);

        while (input.good() && input.tellg() < size) {
            if (source.descriptor_bit() != 0U) {
                // Symbolwise match.
                uint8_t const byte = read1(input);
                write1(dest, byte);
            } else {
                // Dictionary match.
                // Distance and length of match.
                size_t const high   = source.get_byte();
                size_t const low    = source.get_byte();
                diff_t const base   = dest.tellp();
                auto         length = static_cast<diff_t>(((high & 0xFCU) >> 2U) + 1U);
                auto         offset = static_cast<diff_t>(((high & 3U) << 8U) | low);
                // The offset is stored as being absolute within a 0x400-byte
                // buffer, starting at position 0x3C0. We just rebase it around
                // base + 0x3C0u.
                constexpr auto const bias
                        = static_cast<diff_t>(rocket_adaptor::first_match_position);
                constexpr auto const buffer_size
                        = static_cast<diff_t>(rocket_adaptor::search_buf_size);

                offset = ((offset - base - bias) % buffer_size) + base - buffer_size;

                if (offset < 0) {
                    diff_t count = (offset + length < 0) ? length
                                                         : (length - (offset + length));
                    fill_n(ostreambuf_iterator<char>(dest), count, 0x20);
                    length -= count;
                    offset += count;
                }
                for (diff_t csrc = offset; csrc < offset + length; csrc++) {
                    diff_t const pointer = dest.tellp();
                    dest.seekg(csrc);
                    uint8_t const byte = read1(dest);
                    dest.seekp(pointer);
                    write1(dest, byte);
                }
            }
        }
    }

    static void encode(ostream& dest, uint8_t const*& data, size_t const size) {
        using edge_type    = typename rocket_adaptor::edge_type;
        using rock_ostream = lzss_ostream<rocket_adaptor>;

        // Compute optimal Rocket parsing of input file.
        auto         list = find_optimal_lzss_parse(data, size, rocket_adaptor{});
        rock_ostream output(dest);

        // Go through each edge in the optimal path.
        for (auto const& edge : list.parse_list) {
            switch (edge.get_type()) {
            case edge_type::symbolwise:
                output.descriptor_bit(1);
                output.put_byte(edge.get_symbol());
                break;
            case edge_type::dictionary: {
                size_t const length = edge.get_length();
                size_t const dist   = edge.get_distance();
                size_t const position
                        = (edge.get_position() - dist) % rocket_adaptor::search_buf_size;
                output.descriptor_bit(0);
                output.put_byte(((length - 1) << 2U) | (position >> 8U));
                output.put_byte(position);
                break;
            }
            case edge_type::terminator:
                break;
            case edge_type::invalid:
                // This should be unreachable.
                std::cerr << "Compression produced invalid edge type "
                          << static_cast<size_t>(edge.get_type()) << std::endl;
                __builtin_unreachable();
            }
        }
    }
};

bool rocket::decode(istream& source, iostream& dest) {
    auto const   location = source.tellg();
    stringstream input(ios::in | ios::out | ios::binary);
    extract(source, input);

    rocket_internal::decode(input, dest);
    source.seekg(location + input.tellg());
    return true;
}

bool rocket::encode(istream& source, ostream& dest) {
    // We will pre-fill the buffer with 0x3C0 0x20's.
    stringstream input(ios::in | ios::out | ios::binary);
    fill_n(ostreambuf_iterator<char>(input),
           rocket_internal::rocket_adaptor::first_match_position, 0x20);
    // Copy to buffer.
    input << source.rdbuf();
    input.seekg(0);
    return basic_rocket::encode(input, dest);
}

bool rocket::encode(ostream& dest, uint8_t const* data, size_t const size) {
    // Internal buffer.
    stringstream outbuff(ios::in | ios::out | ios::binary);
    rocket_internal::encode(outbuff, data, size);

    // Fill in header
    // Size of decompressed file
    big_endian::write2(
            dest, static_cast<uint16_t>(
                          size - rocket_internal::rocket_adaptor::first_match_position));
    // Size of compressed file
    big_endian::write2(dest, static_cast<uint16_t>(outbuff.tellp()));

    outbuff.seekg(0);
    dest << outbuff.rdbuf();
    return true;
}
