/*
 * Copyright (C) Flamewing 2013-2016 <flamewing.sonic@gmail.com>
 * Very loosely based on code by the KENS Project Development Team
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

#include "mdcomp/saxman.hh"

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
#include <iterator>
#include <limits>
#include <ostream>
#include <span>
#include <sstream>
#include <type_traits>
#include <vector>

template <>
size_t moduled_saxman::pad_mask_bits = 1U;

class saxman_internal {
    // NOTE: This has to be changed for other LZSS-based compression schemes.
    struct saxman_adaptor {
        using stream_t            = uint8_t;
        using stream_endian_t     = big_endian;
        using descriptor_t        = uint8_t;
        using descriptor_endian_t = little_endian;
        using sliding_window_t    = sliding_window<saxman_adaptor>;
        enum class edge_type : uint8_t {
            invalid,
            terminator,
            symbolwise,
            dictionary,
            zerofill
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
        constexpr static size_t const first_match_position = 0;
        // Size of the search buffer.
        constexpr static size_t const search_buf_size = 4096;
        // Size of the look-ahead buffer.
        constexpr static size_t const look_ahead_buf_size = 18;

        // Creates the (multilayer) sliding window structure.
        static auto create_sliding_window(std::span<stream_t const> data) noexcept {
            return std::array{
                    sliding_window_t{
                                     data, search_buf_size, 3, look_ahead_buf_size,
                                     edge_type::dictionary}
            };
        }

        // Given an edge type, computes how many bits are used in the descriptor
        // field.
        constexpr static size_t desc_bits(edge_type const type) noexcept {
            // Saxman always uses a single bit descriptor.
            ignore_unused_variable_warning(type);
            return type == edge_type::terminator ? 0 : 1;
        }

        // Given an edge type, computes how many bits are used in total by this
        // edge. A return of "numeric_limits<size_t>::max()" means "infinite",
        // or "no edge".
        constexpr static size_t edge_weight(
                edge_type const type, size_t length) noexcept {
            ignore_unused_variable_warning(length);
            switch (type) {
                using enum edge_type;
            case terminator:
                // Does not have a terminator.
                return 0;
            case symbolwise:
                // 8-bit value.
                return desc_bits(type) + 8;
            case dictionary:
            case zerofill:
                // 12-bit offset, 4-bit length.
                return desc_bits(type) + 12 + 4;
            case invalid:
                return std::numeric_limits<size_t>::max();
            }
            __builtin_unreachable();
        }

        // Saxman allows encoding of a sequence of zeroes with no previous
        // match.
        static bool extra_matches(
                std::span<stream_t const> data, size_t const base_node,
                size_t const ubound, size_t const lbound,
                std::vector<adj_list_node<saxman_adaptor>>& matches) noexcept {
            using match_t = adj_list_node<saxman_adaptor>::match_info;
            ignore_unused_variable_warning(lbound);
            // Can't encode zero match after this point.
            if (base_node >= search_buf_size - 1) {
                // Do normal matches.
                return false;
            }
            // Try matching zeroes.
            size_t       offset = 0;
            size_t const end    = ubound - base_node;
            while (data[base_node + offset] == 0) {
                if (++offset >= end) {
                    break;
                }
            }
            // Need at least 3 zeroes in sequence.
            if (offset >= 3) {
                // Got them, so add them to the list.
                for (size_t length = 3; length <= offset; length++) {
                    matches.emplace_back(
                            base_node,
                            match_t{std::numeric_limits<size_t>::max(), length},
                            edge_type::zerofill);
                }
            }
            return !matches.empty();
        }

        // Saxman needs no additional padding at the end-of-file.
        constexpr static size_t get_padding(size_t const total_length) noexcept {
            ignore_unused_variable_warning(total_length);
            return 0;
        }
    };

public:
    static void decode(std::istream& input, std::iostream& dest, size_t const size) {
        using sax_istream = lzss_istream<saxman_adaptor>;
        using diff_t      = std::make_signed_t<size_t>;

        sax_istream source(input);
        auto const  ssize = static_cast<diff_t>(size);

        constexpr auto const buffer_size
                = static_cast<diff_t>(saxman_adaptor::search_buf_size);

        // Loop while the file is good and we haven't gone over the declared
        // length.
        while (input.good() && input.tellg() < ssize) {
            if (source.descriptor_bit() != 0U) {
                // Symbolwise match.
                if (input.peek() == std::istream::traits_type::eof()) {
                    break;
                }
                write1(dest, source.get_byte());
            } else {
                if (input.peek() == std::istream::traits_type::eof()) {
                    break;
                }

                // Dictionary match.
                // Offset and length of match.
                size_t const high = source.get_byte();
                size_t const low  = source.get_byte();

                auto const base_offset
                        = static_cast<diff_t>((high | ((low & 0xF0U) << 4U)) + 18)
                          % buffer_size;
                auto const length = static_cast<diff_t>((low & 0xFU) + 3);

                // The offset is stored as being absolute within current
                // 0x1000-byte block, with part of it being remapped to the end
                // of the previous 0x1000-byte block. We just rebase it around
                // base.
                auto const base = dest.tellp();
                auto const offset
                        = ((base_offset - base) % buffer_size) + base - buffer_size;

                if (offset < base) {
                    // If the offset is before the current output position, we
                    // copy bytes from the given location.
                    lzss_copy<saxman_adaptor>(dest, offset, length);
                } else {
                    // Otherwise, it is a zero fill.
                    std::ranges::fill_n(std::ostreambuf_iterator<char>(dest), length, 0);
                }
            }
        }
    }

    static void encode(std::ostream& dest, std::span<uint8_t const> data) {
        using edge_type   = typename saxman_adaptor::edge_type;
        using sax_ostream = lzss_ostream<saxman_adaptor>;

        // Compute optimal Saxman parsing of input file.
        auto        list = find_optimal_lzss_parse(data, saxman_adaptor{});
        sax_ostream output(dest);

        // Go through each edge in the optimal path.
        for (auto const& edge : list.parse_list) {
            switch (edge.get_type()) {
                using enum saxman_adaptor::edge_type;
            case symbolwise:
                output.descriptor_bit(1);
                output.put_byte(edge.get_symbol());
                break;
            case dictionary:
            case zerofill: {
                size_t const length   = edge.get_length();
                size_t const dist     = edge.get_distance();
                size_t const position = edge.get_position();
                size_t const base     = (position - dist - 0x12U) & 0xFFFU;
                size_t const low      = base & 0xFFU;
                size_t const high     = ((length - 3U) & 0x0FU) | ((base >> 4U) & 0xF0U);
                output.descriptor_bit(0);
                output.put_byte(low);
                output.put_byte(high);
                break;
            }
            case terminator:
                break;
            case invalid:
                // This should be unreachable.
                std::cerr << "Compression produced invalid edge type "
                          << static_cast<size_t>(edge.get_type()) << '\n';
                __builtin_unreachable();
            }
        }
    }
};

bool saxman::decode(std::istream& source, std::iostream& dest, size_t size) {
    if (size == 0) {
        size = little_endian::read2(source);
    }

    auto const        location = source.tellg();
    std::stringstream input(std::ios::in | std::ios::out | std::ios::binary);
    extract(source, input);

    saxman_internal::decode(input, dest, size);
    source.seekg(location + input.tellg());
    return true;
}

bool saxman::encode(
        std::ostream& dest, std::span<uint8_t const> data, bool const with_size) {
    std::stringstream out_buff(std::ios::in | std::ios::out | std::ios::binary);
    auto const        start = out_buff.tellg();
    saxman_internal::encode(out_buff, data);
    if (with_size) {
        out_buff.seekg(start);
        out_buff.ignore(std::numeric_limits<std::streamsize>::max());
        auto full_size = out_buff.gcount();
        little_endian::write2(dest, static_cast<uint16_t>(full_size));
    }
    out_buff.seekg(start);
    dest << out_buff.rdbuf();
    return true;
}
