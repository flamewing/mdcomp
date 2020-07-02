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
#include <mdcomp/kosinski.hh>
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
size_t moduled_kosinski::PadMaskBits = 1U;

class kosinski_internal {
    // NOTE: This has to be changed for other LZSS-based compression schemes.
    struct KosinskiAdaptor {
        using stream_t            = uint8_t;
        using stream_endian_t     = BigEndian;
        using descriptor_t        = uint16_t;
        using descriptor_endian_t = LittleEndian;
        using SlidingWindow_t     = SlidingWindow<KosinskiAdaptor>;
        enum class EdgeType : size_t {
            invalid,
            symbolwise,
            dictionary_inline,
            dictionary_short,
            dictionary_long
        };
        // Number of bits on descriptor bitfield.
        constexpr static size_t const NumDescBits = sizeof(descriptor_t) * 8;
        // Number of bits used in descriptor bitfield to signal the end-of-file
        // marker sequence.
        constexpr static size_t const NumTermBits = 2;
        // Number of bits for end-of-file marker.
        constexpr static size_t const TerminatorWeight = NumTermBits + 3 * 8;
        // Flag that tells the compressor that new descriptor fields are needed
        // as soon as the last bit in the previous one is used up.
        constexpr static bool const NeedEarlyDescriptor = true;
        // Flag that marks the descriptor bits as being in little-endian bit
        // order (that is, lowest bits come out first).
        constexpr static bool const DescriptorLittleEndianBits = true;
        // How many characters to skip looking for matchs for at the start.
        constexpr static size_t const FirstMatchPosition = 0;
        // Size of the search buffer.
        constexpr static size_t const SearchBufSize = 8192;
        // Size of the look-ahead buffer.
        constexpr static size_t const LookAheadBufSize = 256;
        // Total size of the sliding window.
        constexpr static size_t const SlidingWindowSize
                = SearchBufSize + LookAheadBufSize;
        // Creates the (multilayer) sliding window structure.
        static auto create_sliding_window(
                stream_t const* dt, size_t const size) noexcept {
            return array<SlidingWindow_t, 3>{
                    SlidingWindow_t{
                            dt, size, 256, 2, 5, EdgeType::dictionary_inline},
                    SlidingWindow_t{
                            dt, size, SearchBufSize, 3, 9,
                            EdgeType::dictionary_short},
                    SlidingWindow_t{
                            dt, size, SearchBufSize, 10, LookAheadBufSize,
                            EdgeType::dictionary_long}};
        }
        // Given an edge type, computes how many bits are used in the descriptor
        // field.
        constexpr static size_t desc_bits(EdgeType const type) noexcept {
            switch (type) {
            case EdgeType::symbolwise:
                // 1-bit describtor.
                return 1;
            case EdgeType::dictionary_inline:
                // 2-bit descriptor, 2-bit count.
                return 2 + 2;
            case EdgeType::dictionary_short:
            case EdgeType::dictionary_long:
                // 2-bit descriptor.
                return 2;
            case EdgeType::invalid:
                return numeric_limits<size_t>::max();
            }
            __builtin_unreachable();
        }
        // Given an edge type, computes how many bits are used in total by this
        // edge. A return of "numeric_limits<size_t>::max()" means "infinite",
        // or "no edge".
        constexpr static size_t
                edge_weight(EdgeType const type, size_t length) noexcept {
            ignore_unused_variable_warning(length);
            switch (type) {
            case EdgeType::symbolwise:
            case EdgeType::dictionary_inline:
                // 8-bit value / distance.
                return desc_bits(type) + 8;
            case EdgeType::dictionary_short:
                // 13-bit distance, 3-bit length.
                return desc_bits(type) + 13 + 3;
            case EdgeType::dictionary_long:
                // 13-bit distance, 3-bit marker (zero),
                // 8-bit length.
                return desc_bits(type) + 13 + 8 + 3;
            case EdgeType::invalid:
                return numeric_limits<size_t>::max();
            }
            __builtin_unreachable();
        }
        // Kosinski finds no additional matches over normal LZSS.
        constexpr static bool extra_matches(
                stream_t const* data, size_t const basenode,
                size_t const ubound, size_t const lbound,
                LZSSGraph<KosinskiAdaptor>::MatchVector& matches) noexcept {
            ignore_unused_variable_warning(
                    data, basenode, ubound, lbound, matches);
            // Do normal matches.
            return false;
        }
        // KosinskiM needs to pad each module to a multiple of 16 bytes.
        static size_t get_padding(size_t const totallen) noexcept {
            return ((totallen + moduled_kosinski::PadMaskBits)
                    & ~moduled_kosinski::PadMaskBits)
                   - totallen;
        }
    };

public:
    static void decode(istream& in, iostream& Dst) {
        using KosIStream = LZSSIStream<KosinskiAdaptor>;

        KosIStream src(in);

        while (in.good()) {
            if (src.descbit() != 0U) {
                // Symbolwise match.
                Write1(Dst, src.getbyte());
            } else {
                // Dictionary matches.
                // Count and distance
                size_t Count    = 0U;
                size_t distance = 0U;

                if (src.descbit() != 0U) {
                    // Separate dictionary match.
                    size_t Low  = src.getbyte();
                    size_t High = src.getbyte();

                    Count = High & 0x07U;

                    if (Count == 0U) {
                        // 3-byte dictionary match.
                        Count = src.getbyte();
                        if (Count == 0U) {
                            break;
                        }
                        if (Count == 1) {
                            continue;
                        }
                        Count += 1;
                    } else {
                        // 2-byte dictionary match.
                        Count += 2;
                    }

                    distance = 0x2000U - (((0xF8U & High) << 5) | Low);
                } else {
                    // Inline dictionary match.
                    size_t High = src.descbit();
                    size_t Low  = src.descbit();

                    Count = ((High << 1) | Low) + 2;

                    distance = 0x100U - src.getbyte();
                }

                for (size_t i = 0; i < Count; i++) {
                    size_t Pointer = Dst.tellp();
                    Dst.seekg(Pointer - distance);
                    uint8_t Byte = Read1(Dst);
                    Dst.seekp(Pointer);
                    Write1(Dst, Byte);
                }
            }
        }
    }

    static void encode(ostream& Dst, uint8_t const* Data, size_t const Size) {
        using EdgeType   = typename KosinskiAdaptor::EdgeType;
        using KosGraph   = LZSSGraph<KosinskiAdaptor>;
        using KosOStream = LZSSOStream<KosinskiAdaptor>;

        // Compute optimal Kosinski parsing of input file.
        KosGraph                   enc(Data, Size);
        typename KosGraph::AdjList list = enc.find_optimal_parse();
        KosOStream                 out(Dst);

        // Go through each edge in the optimal path.
        for (auto const& edge : list) {
            switch (edge.get_type()) {
            case EdgeType::symbolwise:
                out.descbit(1);
                out.putbyte(edge.get_symbol());
                break;
            case EdgeType::dictionary_inline: {
                size_t const len  = edge.get_length() - 2;
                size_t const dist = 0x100U - edge.get_distance();
                out.descbit(0);
                out.descbit(0);
                out.descbit((len >> 1) & 1);
                out.descbit(len & 1);
                out.putbyte(dist);
                break;
            }
            case EdgeType::dictionary_short:
            case EdgeType::dictionary_long: {
                size_t const len  = edge.get_length();
                size_t const dist = 0x2000U - edge.get_distance();
                size_t const high = (dist >> 5) & 0xF8U;
                size_t const low  = (dist & 0xFFU);
                out.descbit(0);
                out.descbit(1);
                if (edge.get_type() == EdgeType::dictionary_short) {
                    out.putbyte(low);
                    out.putbyte(high | (len - 2));
                } else {
                    out.putbyte(low);
                    out.putbyte(high);
                    out.putbyte(len - 1);
                }
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
        out.descbit(0);
        out.descbit(1);

        // Write end-of-file marker. Maybe use 0x00 0xF8 0x00 instead?
        out.putbyte(0x00);
        out.putbyte(0xF0);
        out.putbyte(0x00);
    }
};

bool kosinski::decode(istream& Src, iostream& Dst) {
    size_t const Location = Src.tellg();
    stringstream in(ios::in | ios::out | ios::binary);
    extract(Src, in);

    kosinski_internal::decode(in, Dst);
    Src.seekg(Location + in.tellg());
    return true;
}

bool kosinski::encode(ostream& Dst, uint8_t const* data, size_t const Size) {
    kosinski_internal::encode(Dst, data, Size);
    return true;
}
