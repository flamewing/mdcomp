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

#include <cstdint>
#include <iostream>
#include <istream>
#include <limits>
#include <ostream>
#include <sstream>

using std::array;
using std::ios;
using std::iostream;
using std::istream;
using std::make_signed_t;
using std::numeric_limits;
using std::ostream;
using std::streamsize;
using std::stringstream;

template <>
size_t moduled_lzkn1::PadMaskBits = 1U;

class lzkn1_internal {
    // NOTE: This has to be changed for other LZSS-based compression schemes.
    struct Lzkn1Adaptor {
        using stream_t            = uint8_t;
        using stream_endian_t     = BigEndian;
        using descriptor_t        = uint8_t;
        using descriptor_endian_t = BigEndian;
        using SlidingWindow_t     = SlidingWindow<Lzkn1Adaptor>;
        enum class EdgeType : size_t {
            invalid,
            terminator,
            symbolwise,
            dictionary_short,
            dictionary_long,
            packed_symbolwise
        };
        // Number of bits on descriptor bitfield.
        constexpr static size_t const NumDescBits = sizeof(descriptor_t) * 8;
        // Flag that tells the compressor that new descriptor fields are needed
        // as soon as the last bit in the previous one is used up.
        constexpr static bool const NeedEarlyDescriptor = false;
        // Ordering of bits on descriptor field. Big bit endian order means high
        // order bits come out first.
        constexpr static bit_endian const DescriptorBitOrder = bit_endian::little;
        // How many characters to skip looking for matches for at the start.
        constexpr static size_t const FirstMatchPosition = 0;
        // Size of the search buffer.
        constexpr static size_t const SearchBufSize = 1023;
        // Size of the look-ahead buffer.
        constexpr static size_t const LookAheadBufSize = 33;

        // Creates the (multilayer) sliding window structure.
        static auto create_sliding_window(std::span<stream_t const> data) noexcept {
            return array{
                    SlidingWindow_t{data,            15, 2,                5,EdgeType::dictionary_short                                  },
                    SlidingWindow_t{
                                    data, SearchBufSize, 3, LookAheadBufSize,
                                    EdgeType::dictionary_long}
            };
        }

        // Given an edge type, computes how many bits are used in the descriptor
        // field.
        constexpr static size_t desc_bits(EdgeType const type) noexcept {
            ignore_unused_variable_warning(type);
            return 1;
        }

        // Given an edge type, computes how many bits are used in total by this
        // edge. A return of "numeric_limits<size_t>::max()" means "infinite",
        // or "no edge".
        constexpr static size_t edge_weight(EdgeType const type, size_t length) noexcept {
            switch (type) {
            case EdgeType::symbolwise:
            case EdgeType::terminator:
                // 8-bit value.
                return desc_bits(type) + 8;
            case EdgeType::dictionary_short:
                // 4-bit distance, 2-bit marker (%10),
                // 2-bit length.
                return desc_bits(type) + 4 + 2 + 2;
            case EdgeType::dictionary_long:
                // 10-bit distance, 1-bit marker (%0),
                // 5-bit length.
                return desc_bits(type) + 10 + 1 + 5;
            case EdgeType::packed_symbolwise:
                // 2-bit marker (%11), 6-bit length,
                // length * 8 bits data.
                return desc_bits(type) + 2 + 6 + length * 8;
            case EdgeType::invalid:
                return numeric_limits<size_t>::max();
            }
            __builtin_unreachable();
        }

        // lzkn1 finds no additional matches over normal LZSS.
        static bool extra_matches(
                std::span<stream_t const> data, size_t const base_node,
                size_t const ubound, size_t const lbound,
                std::vector<AdjListNode<Lzkn1Adaptor>>& matches) noexcept {
            using Match_t = AdjListNode<Lzkn1Adaptor>::MatchInfo;
            ignore_unused_variable_warning(data, lbound);
            // Add packed symbolwise matches.
            size_t const end = std::min(ubound - base_node, size_t{72});
            for (size_t ii = 8; ii < end; ii++) {
                matches.emplace_back(
                        base_node, Match_t{numeric_limits<size_t>::max(), ii},
                        EdgeType::packed_symbolwise);
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
    static void decode(istream& input, iostream& Dest) {
        using Lzkn1IStream = LZSSIStream<Lzkn1Adaptor>;

        size_t const UncompressedSize = BigEndian::Read2(input);

        Lzkn1IStream            source(input);
        constexpr uint8_t const eof_marker               = 0x1FU;
        constexpr uint8_t const packed_symbolwise_marker = 0xC0U;
        constexpr uint8_t const short_match_marker       = 0x80U;

        size_t BytesWritten = 0U;

        while (input.good()) {
            if (source.descriptor_bit() == 0U) {
                // Symbolwise match.
                Write1(Dest, source.get_byte());
                BytesWritten++;
            } else {
                // Dictionary matches or packed symbolwise match.
                uint8_t const Data = source.get_byte();
                if (Data == eof_marker) {
                    // Terminator.
                    break;
                }
                if ((Data & packed_symbolwise_marker) == packed_symbolwise_marker) {
                    // Packed symbolwise.
                    size_t const Count = Data - packed_symbolwise_marker + 8U;
                    for (size_t i = 0; i < Count; i++) {
                        Write1(Dest, source.get_byte());
                    }
                    BytesWritten += Count;
                } else {
                    // Dictionary matches.
                    bool const long_match
                            = (Data & short_match_marker) != short_match_marker;
                    size_t         Count    = 0U;
                    std::streamoff distance = 0U;

                    if (long_match) {
                        // Long dictionary match.
                        uint8_t High = Data;
                        uint8_t Low  = source.get_byte();

                        distance = static_cast<std::streamoff>(
                                ((High * 8U) & 0x300U) | Low);
                        Count = (High & 0x1FU) + 3U;
                    } else {
                        // Short dictionary match.
                        distance = Data & 0xFU;
                        Count    = (Data >> 4U) - 6U;
                    }

                    for (size_t i = 0; i < Count; i++) {
                        std::streamsize const Pointer = Dest.tellp();
                        Dest.seekg(Pointer - distance);
                        uint8_t const Byte = Read1(Dest);
                        Dest.seekp(Pointer);
                        Write1(Dest, Byte);
                    }
                    BytesWritten += Count;
                }
            }
        }
        if (BytesWritten != UncompressedSize) {
            std::cerr << "Something went wrong; expected " << UncompressedSize
                      << " bytes, got " << BytesWritten << " bytes instead." << std::endl;
        }
    }

    static void encode(ostream& Dest, uint8_t const* Data, size_t const Size) {
        using EdgeType     = typename Lzkn1Adaptor::EdgeType;
        using Lzkn1OStream = LZSSOStream<Lzkn1Adaptor>;

        BigEndian::Write2(Dest, Size & std::numeric_limits<uint16_t>::max());

        // Compute optimal lzkn1 parsing of input file.
        auto         list = find_optimal_lzss_parse(Data, Size, Lzkn1Adaptor{});
        Lzkn1OStream output(Dest);
        constexpr uint8_t const eof_marker               = 0x1FU;
        constexpr uint8_t const packed_symbolwise_marker = 0xC0U;

        // Go through each edge in the optimal path.
        for (auto const& edge : list.parse_list) {
            switch (edge.get_type()) {
            case EdgeType::symbolwise:
                output.descriptor_bit(0);
                output.put_byte(edge.get_symbol());
                break;
            case EdgeType::packed_symbolwise: {
                output.descriptor_bit(1);
                size_t const  Count    = edge.get_length();
                size_t const  position = edge.get_position();
                uint8_t const data     = (Count + packed_symbolwise_marker - 8U)
                                     & std::numeric_limits<uint8_t>::max();
                output.put_byte(data);
                for (size_t current = position; current < position + Count; current++) {
                    output.put_byte(Data[current]);
                }
                break;
            }
            case EdgeType::dictionary_short: {
                output.descriptor_bit(1);
                size_t const  Count    = edge.get_length();
                size_t const  distance = edge.get_distance();
                uint8_t const data     = (((Count + 6U) << 4U) | distance)
                                     & std::numeric_limits<uint8_t>::max();
                output.put_byte(data);
                break;
            }
            case EdgeType::dictionary_long: {
                output.descriptor_bit(1);
                size_t const  Count    = edge.get_length();
                size_t const  distance = edge.get_distance();
                uint8_t const high     = ((Count - 3U) | ((distance & 0x300U) >> 3U))
                                     & std::numeric_limits<uint8_t>::max();
                uint8_t const low = distance & 0xFFU;
                output.put_byte(high);
                output.put_byte(low);
                break;
            }
            case EdgeType::terminator: {
                // Push descriptor for end-of-file marker.
                output.descriptor_bit(1);
                // Write end-of-file marker.
                output.put_byte(eof_marker);
                break;
            }
            case EdgeType::invalid:
                // This should be unreachable.
                std::cerr << "Compression produced invalid edge type "
                          << static_cast<size_t>(edge.get_type()) << std::endl;
                __builtin_unreachable();
            }
        }
    }
};

bool lzkn1::decode(istream& Source, iostream& Dest) {
    auto const   Location = Source.tellg();
    stringstream input(ios::in | ios::out | ios::binary);
    extract(Source, input);

    lzkn1_internal::decode(input, Dest);
    Source.seekg(Location + input.tellg());
    return true;
}

bool lzkn1::encode(ostream& Dest, uint8_t const* data, size_t const Size) {
    lzkn1_internal::encode(Dest, data, Size);
    return true;
}
