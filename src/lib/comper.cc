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
size_t moduled_comper::PadMaskBits = 1U;

class comper_internal {
    // NOTE: This has to be changed for other LZSS-based compression schemes.
    struct ComperAdaptor {
        using stream_t            = uint16_t;
        using stream_endian_t     = BigEndian;
        using descriptor_t        = uint16_t;
        using descriptor_endian_t = BigEndian;
        using SlidingWindow_t     = SlidingWindow<ComperAdaptor>;
        enum class EdgeType : size_t {
            invalid,
            terminator,
            symbolwise,
            dictionary
        };
        // Number of bits on descriptor bitfield.
        constexpr static size_t const NumDescBits = sizeof(descriptor_t) * 8;
        // Flag that tells the compressor that new descriptor fields is needed
        // when a new bit is needed and all bits in the previous one have been
        // used up.
        constexpr static bool const NeedEarlyDescriptor = false;
        // Ordering of bits on descriptor field. Big bit endian order means high
        // order bits come out first.
        constexpr static bit_endian const DescriptorBitOrder = bit_endian::big;
        // How many characters to skip looking for matchs for at the start.
        constexpr static size_t const FirstMatchPosition = 0;
        // Size of the search buffer.
        constexpr static size_t const SearchBufSize = 256;
        // Size of the look-ahead buffer.
        constexpr static size_t const LookAheadBufSize = 256;

        // Creates the (multilayer) sliding window structure.
        static auto create_sliding_window(std::span<stream_t const> data) noexcept {
            return array{
                    SlidingWindow_t{
                                    data, SearchBufSize, 2, LookAheadBufSize,
                                    EdgeType::dictionary}
            };
        }

        // Given an edge type, computes how many bits are used in the descriptor
        // field.
        constexpr static size_t desc_bits(EdgeType const type) noexcept {
            // Comper always uses a single bit descriptor.
            ignore_unused_variable_warning(type);
            return 1;
        }

        // Given an edge type, computes how many bits are used in total by this
        // edge. A return of "numeric_limits<size_t>::max()" means "infinite",
        // or "no edge".
        constexpr static size_t edge_weight(EdgeType const type, size_t length) noexcept {
            ignore_unused_variable_warning(length);
            switch (type) {
            case EdgeType::symbolwise:
            case EdgeType::terminator:
                // 16-bit value.
                return desc_bits(type) + 16;
            case EdgeType::dictionary:
                // 8-bit distance, 8-bit length.
                return desc_bits(type) + 8 + 8;
            case EdgeType::invalid:
                return numeric_limits<size_t>::max();
            }
            __builtin_unreachable();
        }

        // Comper finds no additional matches over normal LZSS.
        constexpr static bool extra_matches(
                std::span<stream_t const> data, size_t const basenode,
                size_t const ubound, size_t const lbound,
                std::vector<AdjListNode<ComperAdaptor>>& matches) noexcept {
            ignore_unused_variable_warning(data, basenode, ubound, lbound, matches);
            // Do normal matches.
            return false;
        }

        // Comper needs no additional padding at the end-of-file.
        constexpr static size_t get_padding(size_t const totallen) noexcept {
            ignore_unused_variable_warning(totallen);
            return 0;
        }
    };

public:
    static void decode(istream& input, iostream& Dst) {
        using CompIStream = LZSSIStream<ComperAdaptor>;

        CompIStream src(input);

        while (input.good()) {
            if (src.descbit() == 0U) {
                // Symbolwise match.
                BigEndian::Write2(Dst, BigEndian::Read2(input));
            } else {
                // Dictionary match.
                // Distance and length of match.
                auto const   distance = (std::streamoff{0x100} - src.getbyte()) * 2;
                size_t const length   = src.getbyte();
                if (length == 0) {
                    break;
                }

                for (size_t i = 0; i <= length; i++) {
                    std::streamsize const Pointer = Dst.tellp();
                    Dst.seekg(Pointer - distance);
                    uint16_t const Word = BigEndian::Read2(Dst);
                    Dst.seekp(Pointer);
                    BigEndian::Write2(Dst, Word);
                }
            }
        }
    }

    static void encode(ostream& Dst, uint8_t const* Data, size_t const Size) {
        using EdgeType    = typename ComperAdaptor::EdgeType;
        using CompOStream = LZSSOStream<ComperAdaptor>;

        // Compute optimal Comper parsing of input file.
        auto        list = find_optimal_lzss_parse(Data, Size, ComperAdaptor{});
        CompOStream out(Dst);

        // Go through each edge in the optimal path.
        for (auto const& edge : list.parse_list) {
            switch (edge.get_type()) {
            case EdgeType::symbolwise: {
                size_t const value = edge.get_symbol();
                size_t const high  = (value >> 8U) & 0xFFU;
                size_t const low   = (value & 0xFFU);
                out.descbit(0);
                out.putbyte(high);
                out.putbyte(low);
                break;
            }
            case EdgeType::dictionary: {
                size_t const len  = edge.get_length();
                size_t const dist = edge.get_distance();
                out.descbit(1);
                out.putbyte(-dist);
                out.putbyte(len - 1);
                break;
            }
            case EdgeType::terminator: {
                // Push descriptor for end-of-file marker.
                out.descbit(1);
                out.putbyte(0);
                out.putbyte(0);
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

bool comper::decode(istream& Src, iostream& Dst) {
    auto const   Location = Src.tellg();
    stringstream input(ios::in | ios::out | ios::binary);
    extract(Src, input);

    comper_internal::decode(input, Dst);
    Src.seekg(Location + input.tellg());
    return true;
}

bool comper::encode(ostream& Dst, uint8_t const* data, size_t const Size) {
    comper_internal::encode(Dst, data, Size);
    return true;
}
