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
size_t moduled_kosplus::pad_mask_bits = 1U;

class kosplus_internal {
    // NOTE: This has to be changed for other LZSS-based compression schemes.
    struct kos_plus_adaptor {
        using stream_t            = uint8_t;
        using stream_endian_t     = big_endian;
        using descriptor_t        = uint8_t;
        using descriptor_endian_t = little_endian;
        using sliding_window_t    = sliding_window<kos_plus_adaptor>;
        enum class edge_type : uint8_t {
            invalid,
            terminator,
            symbolwise,
            dictionary_inline,
            dictionary_short,
            dictionary_long
        };
        // Number of bits on descriptor bitfield.
        constexpr static size_t const num_desc_bits = sizeof(descriptor_t) * 8;
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

        // Creates the (multilayer) sliding window structure.
        static auto create_sliding_window(std::span<stream_t const> data) noexcept {
            using enum edge_type;
            return std::array{
                    sliding_window_t{data,             256,  2,                   5,dictionary_inline                                                                                    },
                    sliding_window_t{data, search_buf_size,  3,                   9,  dictionary_short},
                    sliding_window_t{
                                     data, search_buf_size, 10, look_ahead_buf_size,
                                     dictionary_long                                                  }
            };
        }

        // Given an edge type, computes how many bits are used in the descriptor
        // field.
        constexpr static size_t desc_bits(edge_type const type) noexcept {
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
            __builtin_unreachable();
        }

        // Given an edge type, computes how many bits are used in total by this
        // edge. A return of "numeric_limits<size_t>::max()" means "infinite",
        // or "no edge".
        constexpr static size_t edge_weight(
                edge_type const type, size_t length) noexcept {
            ignore_unused_variable_warning(length);
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
            __builtin_unreachable();
        }

        // KosPlus finds no additional matches over normal LZSS.
        constexpr static bool extra_matches(
                std::span<stream_t const> data, size_t const base_node,
                size_t const ubound, size_t const lbound,
                std::vector<adj_list_node<kos_plus_adaptor>>& matches) noexcept {
            ignore_unused_variable_warning(data, base_node, ubound, lbound, matches);
            // Do normal matches.
            return false;
        }

        // KosPlusM needs no additional padding at the end-of-file.
        constexpr static size_t get_padding(size_t const total_length) noexcept {
            ignore_unused_variable_warning(total_length);
            return 0;
        }
    };

public:
    static void decode(std::istream& input, std::iostream& dest) {
        using kos_istream = lzss_istream<kos_plus_adaptor>;
        using diff_t      = std::make_signed_t<size_t>;

        kos_istream source(input);

        while (input.good()) {
            if (source.descriptor_bit() != 0U) {
                // Symbolwise match.
                write1(dest, source.get_byte());
            } else {
                // Dictionary matches.
                // Count and distance
                size_t         count    = 0U;
                std::streamoff distance = 0U;

                if (source.descriptor_bit() != 0U) {
                    // Separate dictionary match.
                    uint8_t const high = source.get_byte();
                    uint8_t const low  = source.get_byte();

                    count = high & 0x07U;

                    if (count == 0U) {
                        // 3-byte dictionary match.
                        count = source.get_byte();
                        if (count == 0U) {
                            break;
                        }
                        count += 9;
                    } else {
                        // 2-byte dictionary match.
                        count = 10 - count;
                    }

                    distance = std::streamoff{0x2000} - (((0xF8U & high) << 5U) | low);
                } else {
                    // Inline dictionary match.
                    distance = std::streamoff{0x100} - source.get_byte();

                    size_t const high = source.descriptor_bit();
                    size_t const low  = source.descriptor_bit();

                    count = ((high << 1U) | low) + 2;
                }

                auto const   length = static_cast<diff_t>(count);
                diff_t const offset = dest.tellp() - distance;
                lzss_copy<kos_plus_adaptor>(dest, offset, length);
            }
        }
    }

    static void encode(std::ostream& dest, std::span<uint8_t const> data) {
        using edge_type   = typename kos_plus_adaptor::edge_type;
        using kos_ostream = lzss_ostream<kos_plus_adaptor>;

        // Compute optimal KosPlus parsing of input file.
        auto        list = find_optimal_lzss_parse(data, kos_plus_adaptor{});
        kos_ostream output(dest);

        // Go through each edge in the optimal path.
        for (auto const& edge : list.parse_list) {
            switch (edge.get_type()) {
            case edge_type::symbolwise:
                output.descriptor_bit(1);
                output.put_byte(edge.get_symbol());
                break;
            case edge_type::dictionary_inline: {
                size_t const length = edge.get_length() - 2;
                size_t const dist   = 0x100U - edge.get_distance();
                output.descriptor_bit(0);
                output.descriptor_bit(0);
                output.put_byte(dist);
                output.descriptor_bit((length >> 1U) & 1U);
                output.descriptor_bit(length & 1U);
                break;
            }
            case edge_type::dictionary_short:
            case edge_type::dictionary_long: {
                size_t const length = edge.get_length();
                size_t const dist   = 0x2000U - edge.get_distance();
                size_t const high   = (dist >> 5U) & 0xF8U;
                size_t const low    = (dist & 0xFFU);
                output.descriptor_bit(0);
                output.descriptor_bit(1);
                if (edge.get_type() == edge_type::dictionary_short) {
                    output.put_byte(high | (10 - length));
                    output.put_byte(low);
                } else {
                    output.put_byte(high);
                    output.put_byte(low);
                    output.put_byte(length - 9);
                }
                break;
            }
            case edge_type::terminator: {
                // Push descriptor for end-of-file marker.
                output.descriptor_bit(0);
                output.descriptor_bit(1);
                // Write end-of-file marker.
                output.put_byte(0xF0);
                output.put_byte(0x00);
                output.put_byte(0x00);
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
