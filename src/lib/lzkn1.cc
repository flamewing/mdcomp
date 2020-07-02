/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
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

#include <mdcomp/bigendian_io.hh>
#include <mdcomp/bitstream.hh>
#include <mdcomp/ignore_unused_variable_warning.hh>
#include <mdcomp/lzkn1.hh>
#include <mdcomp/lzss.hh>

#include <cstdint>
#include <iostream>
#include <istream>
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
            symbolwise,
            dictionary_short,
            dictionary_long,
            packed_symbolwise
        };
        // Number of bits on descriptor bitfield.
        constexpr static size_t const NumDescBits = sizeof(descriptor_t) * 8;
        // Number of bits used in descriptor bitfield to signal the end-of-file
        // marker sequence.
        constexpr static size_t const NumTermBits = 1;
        // Number of bits for end-of-file marker.
        constexpr static size_t const TerminatorWeight = NumTermBits + 8;
        // Flag that tells the compressor that new descriptor fields are needed
        // as soon as the last bit in the previous one is used up.
        constexpr static bool const NeedEarlyDescriptor = false;
        // Flag that marks the descriptor bits as being in little-endian bit
        // order (that is, lowest bits come out first).
        constexpr static bool const DescriptorLittleEndianBits = true;
        // How many characters to skip looking for matchs for at the start.
        constexpr static size_t const FirstMatchPosition = 0;
        // Size of the search buffer.
        constexpr static size_t const SearchBufSize = 1023;
        // Size of the look-ahead buffer.
        constexpr static size_t const LookAheadBufSize = 33;
        // Total size of the sliding window.
        constexpr static size_t const SlidingWindowSize
                = SearchBufSize + LookAheadBufSize;
        // Creates the (multilayer) sliding window structure.
        static auto create_sliding_window(
                stream_t const* dt, size_t const size) noexcept {
            return array<SlidingWindow_t, 2>{
                    SlidingWindow_t{
                            dt, size, 15, 2, 5, EdgeType::dictionary_short},
                    SlidingWindow_t{
                            dt, size, SearchBufSize, 3, LookAheadBufSize,
                            EdgeType::dictionary_long}};
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
        constexpr static size_t
                edge_weight(EdgeType const type, size_t length) noexcept {
            switch (type) {
            case EdgeType::symbolwise:
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
                stream_t const* data, size_t const basenode,
                size_t const ubound, size_t const lbound,
                LZSSGraph<Lzkn1Adaptor>::MatchVector& matches) noexcept {
            ignore_unused_variable_warning(data, lbound);
            // Add packed symbolwise matches.
            size_t const end = std::min(ubound - basenode, size_t(72));
            for (size_t ii = 8; ii < end; ii++) {
                matches.emplace_back(
                        basenode, numeric_limits<size_t>::max(), ii,
                        EdgeType::packed_symbolwise);
            }
            // Do normal matches.
            return false;
        }
        // lzkn1M needs to pad each module to a multiple of 16 bytes.
        static size_t get_padding(size_t const totallen) noexcept {
            ignore_unused_variable_warning(totallen);
            return 0;
        }
    };

public:
    static void decode(istream& in, iostream& Dst) {
        using Lzkn1IStream = LZSSIStream<Lzkn1Adaptor>;

        size_t const UncompressedSize = BigEndian::Read2(in);

        Lzkn1IStream           src(in);
        constexpr size_t const eof_marker               = 0x1FU;
        constexpr size_t const packed_symbolwise_marker = 0xC0U;
        constexpr size_t const short_match_marker       = 0x80U;

        size_t BytesWritten = 0U;

        while (in.good()) {
            if (src.descbit() == 0U) {
                // Symbolwise match.
                Write1(Dst, src.getbyte());
                BytesWritten++;
            } else {
                // Dictionary matches or packed symbolwise match.
                size_t const Data = src.getbyte();
                if (Data == eof_marker) {
                    // Terminator.
                    break;
                }
                if ((Data & packed_symbolwise_marker)
                    == packed_symbolwise_marker) {
                    // Packed symbolwise.
                    size_t const Count = Data - packed_symbolwise_marker + 8U;
                    for (size_t i = 0; i < Count; i++) {
                        Write1(Dst, src.getbyte());
                    }
                    BytesWritten += Count;
                } else {
                    // Dictionary matches.
                    bool const long_match
                            = (Data & short_match_marker) != short_match_marker;
                    size_t Count    = 0U;
                    size_t distance = 0U;

                    if (long_match == true) {
                        // Long dictionary match.
                        size_t High = Data;
                        size_t Low  = src.getbyte();

                        distance = ((High << 3) & 0x300U) | Low;
                        Count    = (High & 0x1FU) + 3U;
                    } else {
                        // Short dictionary match.
                        distance = Data & 0xFU;
                        Count    = (Data >> 4) - 6U;
                    }

                    for (size_t i = 0; i < Count; i++) {
                        size_t Pointer = Dst.tellp();
                        Dst.seekg(Pointer - distance);
                        uint8_t Byte = Read1(Dst);
                        Dst.seekp(Pointer);
                        Write1(Dst, Byte);
                    }
                    BytesWritten += Count;
                }
            }
        }
        if (BytesWritten != UncompressedSize) {
            std::cerr << "Something went wrong; expected " << UncompressedSize
                      << " bytes, got " << BytesWritten << " bytes instead."
                      << std::endl;
        }
    }

    static void encode(ostream& Dst, uint8_t const* Data, size_t const Size) {
        using EdgeType     = typename Lzkn1Adaptor::EdgeType;
        using Lzkn1Graph   = LZSSGraph<Lzkn1Adaptor>;
        using Lzkn1OStream = LZSSOStream<Lzkn1Adaptor>;

        BigEndian::Write2(Dst, Size);

        // Compute optimal lzkn1 parsing of input file.
        Lzkn1Graph                   enc(Data, Size);
        typename Lzkn1Graph::AdjList list = enc.find_optimal_parse();
        Lzkn1OStream                 out(Dst);
        constexpr size_t const       eof_marker               = 0x1FU;
        constexpr size_t const       packed_symbolwise_marker = 0xC0U;

        // Go through each edge in the optimal path.
        for (auto const& edge : list) {
            switch (edge.get_type()) {
            case EdgeType::symbolwise:
                out.descbit(0);
                out.putbyte(edge.get_symbol());
                break;
            case EdgeType::packed_symbolwise: {
                out.descbit(1);
                size_t const  Count    = edge.get_length();
                size_t const  position = edge.get_pos();
                uint8_t const data     = Count + packed_symbolwise_marker - 8;
                out.putbyte(data);
                for (size_t currpos = position; currpos < position + Count;
                     currpos++) {
                    out.putbyte(Data[currpos]);
                }
                break;
            }
            case EdgeType::dictionary_short: {
                out.descbit(1);
                size_t const  Count    = edge.get_length();
                size_t const  distance = edge.get_distance();
                uint8_t const data     = ((Count + 6U) << 4) | distance;
                out.putbyte(data);
                break;
            }
            case EdgeType::dictionary_long: {
                out.descbit(1);
                size_t const  Count    = edge.get_length();
                size_t const  distance = edge.get_distance();
                uint8_t const high = (Count - 3U) | ((distance & 0x300U) >> 3);
                uint8_t const low  = distance & 0xFFU;
                out.putbyte(high);
                out.putbyte(low);
                break;
            }
            case EdgeType::invalid:
                // This should be unreachable.
                std::cerr << "Compression produced invalid edge type "
                          << static_cast<size_t>(edge.get_type()) << std::endl;
                __builtin_unreachable();
            }
        }

        // Push descriptor for end-of-file marker.
        out.descbit(1);

        // Write end-of-file marker.
        out.putbyte(eof_marker);
    }
};

bool lzkn1::decode(istream& Src, iostream& Dst) {
    size_t const Location = Src.tellg();
    stringstream in(ios::in | ios::out | ios::binary);
    extract(Src, in);

    lzkn1_internal::decode(in, Dst);
    Src.seekg(Location + in.tellg());
    return true;
}

bool lzkn1::encode(ostream& Dst, uint8_t const* data, size_t const Size) {
    lzkn1_internal::encode(Dst, data, Size);
    return true;
}
