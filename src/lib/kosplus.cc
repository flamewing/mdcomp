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

#include <mdcomp/bigendian_io.hh>
#include <mdcomp/bitstream.hh>
#include <mdcomp/ignore_unused_variable_warning.hh>
#include <mdcomp/kosplus.hh>
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
size_t moduled_kosplus::PadMaskBits = 1U;

class kosplus_internal {
    // NOTE: This has to be changed for other LZSS-based compression schemes.
    struct KosPlusAdaptor {
        using stream_t            = uint8_t;
        using stream_endian_t     = BigEndian;
        using descriptor_t        = uint8_t;
        using descriptor_endian_t = LittleEndian;
        using SlidingWindow_t     = SlidingWindow<KosPlusAdaptor>;
        enum class EdgeType : size_t {
            invalid,
            terminator,
            symbolwise,
            dictionary_inline,
            dictionary_short,
            dictionary_long
        };
        // Number of bits on descriptor bitfield.
        constexpr static size_t const NumDescBits = sizeof(descriptor_t) * 8;
        // Flag that tells the compressor that new descriptor fields are needed
        // as soon as the last bit in the previous one is used up.
        constexpr static bool const NeedEarlyDescriptor = false;
        // Ordering of bits on descriptor field. Big bit endian order means high
        // order bits come out first.
        constexpr static bit_endian const DescriptorBitOrder = bit_endian::big;
        // How many characters to skip looking for matchs for at the start.
        constexpr static size_t const FirstMatchPosition = 0;
        // Size of the search buffer.
        constexpr static size_t const SearchBufSize = 8192;
        // Size of the look-ahead buffer.
        constexpr static size_t const LookAheadBufSize = 264;
        // Creates the (multilayer) sliding window structure.
        static auto create_sliding_window(
                stream_t const* dt, size_t const size) noexcept {
            return array<SlidingWindow_t, 3>{
                    SlidingWindow_t{dt, size, 256, 2, 5, EdgeType::dictionary_inline},
                    SlidingWindow_t{
                            dt, size, SearchBufSize, 3, 9, EdgeType::dictionary_short},
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
            case EdgeType::terminator:
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
        constexpr static size_t edge_weight(EdgeType const type, size_t length) noexcept {
            ignore_unused_variable_warning(length);
            switch (type) {
            case EdgeType::symbolwise:
            case EdgeType::dictionary_inline:
                // 8-bit value/distance.
                return desc_bits(type) + 8;
            case EdgeType::dictionary_short:
                // 13-bit distance, 3-bit length.
                return desc_bits(type) + 13 + 3;
            case EdgeType::dictionary_long:
                // 13-bit distance, 3-bit marker (zero),
                // 8-bit length.
                return desc_bits(type) + 13 + 8 + 3;
            case EdgeType::terminator:
                // 24-bit value.
                return desc_bits(type) + 24;
            case EdgeType::invalid:
                return numeric_limits<size_t>::max();
            }
            __builtin_unreachable();
        }
        // KosPlus finds no additional matches over normal LZSS.
        constexpr static bool extra_matches(
                stream_t const* data, size_t const basenode, size_t const ubound,
                size_t const                              lbound,
                std::vector<AdjListNode<KosPlusAdaptor>>& matches) noexcept {
            ignore_unused_variable_warning(data, basenode, ubound, lbound, matches);
            // Do normal matches.
            return false;
        }
        // KosPlusM needs no additional padding at the end-of-file.
        constexpr static size_t get_padding(size_t const totallen) noexcept {
            ignore_unused_variable_warning(totallen);
            return 0;
        }
    };

public:
    static void decode(istream& in, iostream& Dst) {
        using KosIStream = LZSSIStream<KosPlusAdaptor>;

        KosIStream src(in);

        while (in.good()) {
            if (src.descbit() != 0U) {
                // Symbolwise match.
                Write1(Dst, src.getbyte());
            } else {
                // Dictionary matches.
                // Count and distance
                size_t         Count    = 0U;
                std::streamoff distance = 0U;

                if (src.descbit() != 0U) {
                    // Separate dictionary match.
                    uint8_t High = src.getbyte();
                    uint8_t Low  = src.getbyte();

                    Count = High & 0x07U;

                    if (Count == 0U) {
                        // 3-byte dictionary match.
                        Count = src.getbyte();
                        if (Count == 0U) {
                            break;
                        }
                        Count += 9;
                    } else {
                        // 2-byte dictionary match.
                        Count = 10 - Count;
                    }

                    distance = std::streamoff{0x2000} - (((0xF8U & High) << 5U) | Low);
                } else {
                    // Inline dictionary match.
                    distance = std::streamoff{0x100} - src.getbyte();

                    size_t High = src.descbit();
                    size_t Low  = src.descbit();

                    Count = ((High << 1U) | Low) + 2;
                }

                for (size_t i = 0; i < Count; i++) {
                    std::streamsize const Pointer = Dst.tellp();
                    Dst.seekg(Pointer - distance);
                    uint8_t const Byte = Read1(Dst);
                    Dst.seekp(Pointer);
                    Write1(Dst, Byte);
                }
            }
        }
    }

    static void encode(ostream& Dst, uint8_t const*& Data, size_t const Size) {
        using EdgeType   = typename KosPlusAdaptor::EdgeType;
        using KosOStream = LZSSOStream<KosPlusAdaptor>;

        // Compute optimal KosPlus parsing of input file.
        auto       list = find_optimal_lzss_parse(Data, Size, KosPlusAdaptor{});
        KosOStream out(Dst);

        // Go through each edge in the optimal path.
        for (auto const& edge : list.parse_list) {
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
                out.putbyte(dist);
                out.descbit((len >> 1U) & 1U);
                out.descbit(len & 1U);
                break;
            }
            case EdgeType::dictionary_short:
            case EdgeType::dictionary_long: {
                size_t const len  = edge.get_length();
                size_t const dist = 0x2000U - edge.get_distance();
                size_t       high = (dist >> 5U) & 0xF8U;
                size_t       low  = (dist & 0xFFU);
                out.descbit(0);
                out.descbit(1);
                if (edge.get_type() == EdgeType::dictionary_short) {
                    out.putbyte(high | (10 - len));
                    out.putbyte(low);
                } else {
                    out.putbyte(high);
                    out.putbyte(low);
                    out.putbyte(len - 9);
                }
                break;
            }
            case EdgeType::terminator: {
                // Push descriptor for end-of-file marker.
                out.descbit(0);
                out.descbit(1);
                // Write end-of-file marker.
                out.putbyte(0xF0);
                out.putbyte(0x00);
                out.putbyte(0x00);
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

bool kosplus::decode(istream& Src, iostream& Dst) {
    auto const   Location = Src.tellg();
    stringstream in(ios::in | ios::out | ios::binary);
    extract(Src, in);

    kosplus_internal::decode(in, Dst);
    Src.seekg(Location + in.tellg());
    return true;
}

bool kosplus::encode(ostream& Dst, uint8_t const* data, size_t const Size) {
    kosplus_internal::encode(Dst, data, Size);
    return true;
}
