/*
 * Copyright (C) Flamewing 2011-2016 <flamewing.sonic@gmail.com>
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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <istream>
#include <limits>
#include <ostream>
#include <span>
#include <sstream>
#include <type_traits>
#include <vector>

template <>
size_t moduled_lzkn1::pad_mask_bits = 1U;

class lzkn1_internal {
    // NOTE: This has to be changed for other LZSS-based compression schemes.
    struct lzkn1_adaptor {
        using stream_t            = uint8_t;
        using stream_endian_t     = big_endian;
        using descriptor_t        = uint8_t;
        using descriptor_endian_t = big_endian;
        using sliding_window_t    = sliding_window<lzkn1_adaptor>;
        enum class edge_type : uint8_t {
            invalid,
            terminator,
            symbolwise,
            dictionary_short,
            dictionary_long,
            packed_symbolwise
        };
        // Number of bits on descriptor bitfield.
        constexpr static size_t const num_desc_bits = sizeof(descriptor_t) * 8;
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

        // Creates the (multilayer) sliding window structure.
        static auto create_sliding_window(std::span<stream_t const> data) noexcept {
            return std::array{
                    sliding_window_t{data,              15, 2,                   5,edge_type::dictionary_short                                   },
                    sliding_window_t{
                                     data, search_buf_size, 3, look_ahead_buf_size,
                                     edge_type::dictionary_long}
            };
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
        constexpr static size_t edge_weight(
                edge_type const type, size_t length) noexcept {
            // NOLINTNEXTLINE(clang-diagnostic-switch-default)
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
                return desc_bits(type) + 2 + 6 + length * 8;
            case invalid:
                return std::numeric_limits<size_t>::max();
            }
            __builtin_unreachable();
        }

        // lzkn1 finds no additional matches over normal LZSS.
        static bool extra_matches(
                std::span<stream_t const> data, size_t const base_node,
                size_t const ubound, size_t const lbound,
                std::vector<adj_list_node<lzkn1_adaptor>>& matches) noexcept {
            using match_t = adj_list_node<lzkn1_adaptor>::match_info;
            ignore_unused_variable_warning(data, lbound);
            // Add packed symbolwise matches.
            size_t const end = std::min(ubound - base_node, size_t{72});
            for (size_t ii = 8; ii < end; ii++) {
                matches.emplace_back(
                        base_node, match_t{std::numeric_limits<size_t>::max(), ii},
                        edge_type::packed_symbolwise);
            }
            // Do normal matches.
            return false;
        }

        // lzkn1M needs to pad each module to a multiple of 16 bytes.
        static size_t get_padding(size_t const total_length) noexcept {
            ignore_unused_variable_warning(total_length);
            return 0;
        }
    };

public:
    static void decode(std::istream& input, std::iostream& dest) {
        using lzkn1_istream = lzss_istream<lzkn1_adaptor>;
        using diff_t        = std::make_signed_t<size_t>;

        size_t const uncompressed_size = big_endian::read2(input);

        lzkn1_istream           source(input);
        constexpr uint8_t const eof_marker               = 0x1FU;
        constexpr uint8_t const packed_symbolwise_marker = 0xC0U;
        constexpr uint8_t const short_match_marker       = 0x80U;

        size_t bytes_written = 0U;

        while (input.good()) {
            if (source.descriptor_bit() == 0U) {
                // Symbolwise match.
                write1(dest, source.get_byte());
                bytes_written++;
            } else {
                // Dictionary matches or packed symbolwise match.
                uint8_t const data = source.get_byte();
                if (data == eof_marker) {
                    // Terminator.
                    break;
                }
                if ((data & packed_symbolwise_marker) == packed_symbolwise_marker) {
                    // Packed symbolwise.
                    size_t const count = data - packed_symbolwise_marker + 8U;
                    for (size_t i = 0; i < count; i++) {
                        write1(dest, source.get_byte());
                    }
                    bytes_written += count;
                } else {
                    // Dictionary matches.
                    bool const long_match
                            = (data & short_match_marker) != short_match_marker;
                    size_t         count    = 0U;
                    std::streamoff distance = 0U;

                    if (long_match) {
                        // Long dictionary match.
                        uint8_t const high = data;
                        uint8_t const low  = source.get_byte();

                        distance = static_cast<std::streamoff>(
                                ((high * 8U) & 0x300U) | low);
                        count = (high & 0x1FU) + 3U;
                    } else {
                        // Short dictionary match.
                        distance = data & 0xFU;
                        count    = (data >> 4U) - 6U;
                    }

                    auto const   length = static_cast<diff_t>(count);
                    diff_t const offset = dest.tellp() - distance;
                    lzss_copy<lzkn1_adaptor>(dest, offset, length);
                    bytes_written += count;
                }
            }
        }
        if (bytes_written != uncompressed_size) {
            std::cerr << "Something went wrong; expected " << uncompressed_size
                      << " bytes, got " << bytes_written << " bytes instead.\n";
        }
    }

    static void encode(std::ostream& dest, std::span<uint8_t const> data) {
        using edge_type     = typename lzkn1_adaptor::edge_type;
        using lzkn1_ostream = lzss_ostream<lzkn1_adaptor>;

        big_endian::write2(dest, data.size() & std::numeric_limits<uint16_t>::max());

        // Compute optimal lzkn1 parsing of input file.
        auto          list = find_optimal_lzss_parse(data, lzkn1_adaptor{});
        lzkn1_ostream output(dest);

        constexpr uint8_t const eof_marker               = 0x1FU;
        constexpr uint8_t const packed_symbolwise_marker = 0xC0U;

        // Go through each edge in the optimal path.
        for (auto const& edge : list.parse_list) {
            // NOLINTNEXTLINE(clang-diagnostic-switch-default)
            switch (edge.get_type()) {
            case edge_type::symbolwise:
                output.descriptor_bit(0);
                output.put_byte(edge.get_symbol());
                break;
            case edge_type::packed_symbolwise: {
                output.descriptor_bit(1);
                size_t const  count    = edge.get_length();
                size_t const  position = edge.get_position();
                uint8_t const value    = (count + packed_symbolwise_marker - 8U)
                                      & std::numeric_limits<uint8_t>::max();
                output.put_byte(value);
                for (size_t current = position; current < position + count; current++) {
                    output.put_byte(data[current]);
                }
                break;
            }
            case edge_type::dictionary_short: {
                output.descriptor_bit(1);
                size_t const  count    = edge.get_length();
                size_t const  distance = edge.get_distance();
                uint8_t const value    = (((count + 6U) << 4U) | distance)
                                      & std::numeric_limits<uint8_t>::max();
                output.put_byte(value);
                break;
            }
            case edge_type::dictionary_long: {
                output.descriptor_bit(1);
                size_t const  count    = edge.get_length();
                size_t const  distance = edge.get_distance();
                uint8_t const high     = ((count - 3U) | ((distance & 0x300U) >> 3U))
                                     & std::numeric_limits<uint8_t>::max();
                uint8_t const low = distance & 0xFFU;
                output.put_byte(high);
                output.put_byte(low);
                break;
            }
            case edge_type::terminator: {
                // Push descriptor for end-of-file marker.
                output.descriptor_bit(1);
                // Write end-of-file marker.
                output.put_byte(eof_marker);
                break;
            }
            case edge_type::invalid:
                // This should be unreachable.
                std::cerr << "Compression produced invalid edge type "
                          << static_cast<size_t>(edge.get_type()) << '\n';
                __builtin_unreachable();
            }
        }
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

bool lzkn1::encode(std::ostream& dest, std::span<uint8_t const> data) {
    lzkn1_internal::encode(dest, data);
    return true;
}
