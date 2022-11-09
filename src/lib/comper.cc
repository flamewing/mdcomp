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

#include "mdcomp/comper.hh"

#include "mdcomp/bigendian_io.hh"
#include "mdcomp/bitstream.hh"
#include "mdcomp/ignore_unused_variable_warning.hh"
#include "mdcomp/lzss.hh"

#include <cstdint>
#include <iostream>
#include <istream>
#include <ostream>
#include <sstream>

template <>
size_t moduled_comper::pad_mask_bits = 1U;

class comper_internal {
    // NOTE: This has to be changed for other LZSS-based compression schemes.
    struct comper_adaptor {
        using stream_t            = uint16_t;
        using stream_endian_t     = big_endian;
        using descriptor_t        = uint16_t;
        using descriptor_endian_t = big_endian;
        using sliding_window_t    = sliding_window<comper_adaptor>;
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
        constexpr static bit_endian const descriptor_bit_order = bit_endian::big;
        // How many characters to skip looking for matches for at the start.
        constexpr static size_t const first_match_position = 0;
        // Size of the search buffer.
        constexpr static size_t const search_buf_size = 256;
        // Size of the look-ahead buffer.
        constexpr static size_t const look_ahead_buf_size = 256;

        // Creates the (multilayer) sliding window structure.
        static auto create_sliding_window(std::span<stream_t const> data) noexcept {
            return std::array{
                    sliding_window_t{
                                     data, search_buf_size, 2, look_ahead_buf_size,
                                     edge_type::dictionary}
            };
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
        constexpr static size_t edge_weight(
                edge_type const type, size_t length) noexcept {
            ignore_unused_variable_warning(length);
            switch (type) {
            case edge_type::symbolwise:
            case edge_type::terminator:
                // 16-bit value.
                return desc_bits(type) + 16;
            case edge_type::dictionary:
                // 8-bit distance, 8-bit length.
                return desc_bits(type) + 8 + 8;
            case edge_type::invalid:
                return std::numeric_limits<size_t>::max();
            }
            __builtin_unreachable();
        }

        // Comper finds no additional matches over normal LZSS.
        constexpr static bool extra_matches(
                std::span<stream_t const> data, size_t const base_node,
                size_t const ubound, size_t const lbound,
                std::vector<adj_list_node<comper_adaptor>>& matches) noexcept {
            ignore_unused_variable_warning(data, base_node, ubound, lbound, matches);
            // Do normal matches.
            return false;
        }

        // Comper needs no additional padding at the end-of-file.
        constexpr static size_t get_padding(size_t const total_length) noexcept {
            ignore_unused_variable_warning(total_length);
            return 0;
        }
    };

public:
    static void decode(std::istream& input, std::iostream& dest) {
        using comp_istream = lzss_istream<comper_adaptor>;

        comp_istream source(input);

        while (input.good()) {
            if (source.descriptor_bit() == 0U) {
                // Symbolwise match.
                big_endian::write2(dest, big_endian::read2(input));
            } else {
                // Dictionary match.
                // Distance and length of match.
                auto const   distance = (std::streamoff{0x100} - source.get_byte()) * 2;
                size_t const length   = source.get_byte();
                if (length == 0) {
                    break;
                }

                for (size_t i = 0; i <= length; i++) {
                    std::streamsize const pointer = dest.tellp();
                    dest.seekg(pointer - distance);
                    uint16_t const word = big_endian::read2(dest);
                    dest.seekp(pointer);
                    big_endian::write2(dest, word);
                }
            }
        }
    }

    static void encode(std::ostream& dest, std::span<uint8_t const> data) {
        using edge_type    = typename comper_adaptor::edge_type;
        using comp_ostream = lzss_ostream<comper_adaptor>;

        // Compute optimal Comper parsing of input file.
        auto         list = find_optimal_lzss_parse(data, comper_adaptor{});
        comp_ostream output(dest);

        // Go through each edge in the optimal path.
        for (auto const& edge : list.parse_list) {
            switch (edge.get_type()) {
            case edge_type::symbolwise: {
                size_t const value = edge.get_symbol();
                size_t const high  = (value >> 8U) & 0xFFU;
                size_t const low   = (value & 0xFFU);
                output.descriptor_bit(0);
                output.put_byte(high);
                output.put_byte(low);
                break;
            }
            case edge_type::dictionary: {
                size_t const length = edge.get_length();
                size_t const dist   = edge.get_distance();
                output.descriptor_bit(1);
                output.put_byte(-dist);
                output.put_byte(length - 1);
                break;
            }
            case edge_type::terminator: {
                // Push descriptor for end-of-file marker.
                output.descriptor_bit(1);
                output.put_byte(0);
                output.put_byte(0);
                break;
            }
            case edge_type::invalid:
                // This should be unreachable.
                std::cerr << "Compression produced invalid edge type "
                          << static_cast<size_t>(edge.get_type()) << std::endl;
                __builtin_unreachable();
            }
        }
    }
};

bool comper::decode(std::istream& source, std::iostream& dest) {
    auto const        location = source.tellg();
    std::stringstream input(std::ios::in | std::ios::out | std::ios::binary);
    extract(source, input);

    comper_internal::decode(input, dest);
    source.seekg(location + input.tellg());
    return true;
}

bool comper::encode(std::ostream& dest, std::span<uint8_t const> data) {
    comper_internal::encode(dest, data);
    return true;
}
