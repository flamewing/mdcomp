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

#include <cstdint>
#include <iostream>
#include <istream>
#include <limits>
#include <ostream>
#include <sstream>

using std::array;
using std::fill_n;
using std::ios;
using std::iostream;
using std::istream;
using std::numeric_limits;
using std::ostream;
using std::ostreambuf_iterator;
using std::streamsize;
using std::stringstream;

template <>
size_t moduled_saxman::PadMaskBits = 1U;

class saxman_internal {
    // NOTE: This has to be changed for other LZSS-based compression schemes.
    struct SaxmanAdaptor {
        using stream_t            = uint8_t;
        using stream_endian_t     = BigEndian;
        using descriptor_t        = uint8_t;
        using descriptor_endian_t = LittleEndian;
        using SlidingWindow_t     = SlidingWindow<SaxmanAdaptor>;
        enum class EdgeType : size_t {
            invalid,
            terminator,
            symbolwise,
            dictionary,
            zerofill
        };
        // Number of bits on descriptor bitfield.
        constexpr static size_t const NumDescBits = sizeof(descriptor_t) * 8;
        // Flag that tells the compressor that new descriptor fields is needed
        // when a new bit is needed and all bits in the previous one have been
        // used up.
        constexpr static bool const NeedEarlyDescriptor = false;
        // Ordering of bits on descriptor field. Big bit endian order means high
        // order bits come out first.
        constexpr static bit_endian const DescriptorBitOrder = bit_endian::little;
        // How many characters to skip looking for matches for at the start.
        constexpr static size_t const FirstMatchPosition = 0;
        // Size of the search buffer.
        constexpr static size_t const SearchBufSize = 4096;
        // Size of the look-ahead buffer.
        constexpr static size_t const LookAheadBufSize = 18;

        // Creates the (multilayer) sliding window structure.
        static auto create_sliding_window(std::span<stream_t const> data) noexcept {
            return array{
                    SlidingWindow_t{
                                    data, SearchBufSize, 3, LookAheadBufSize,
                                    EdgeType::dictionary}
            };
        }

        // Given an edge type, computes how many bits are used in the descriptor
        // field.
        constexpr static size_t desc_bits(EdgeType const type) noexcept {
            // Saxman always uses a single bit descriptor.
            ignore_unused_variable_warning(type);
            return type == EdgeType::terminator ? 0 : 1;
        }

        // Given an edge type, computes how many bits are used in total by this
        // edge. A return of "numeric_limits<size_t>::max()" means "infinite",
        // or "no edge".
        constexpr static size_t edge_weight(EdgeType const type, size_t length) noexcept {
            ignore_unused_variable_warning(length);
            switch (type) {
            case EdgeType::terminator:
                // Does not have a terminator.
                return 0;
            case EdgeType::symbolwise:
                // 8-bit value.
                return desc_bits(type) + 8;
            case EdgeType::dictionary:
            case EdgeType::zerofill:
                // 12-bit offset, 4-bit length.
                return desc_bits(type) + 12 + 4;
            case EdgeType::invalid:
                return numeric_limits<size_t>::max();
            }
            __builtin_unreachable();
        }

        // Saxman allows encoding of a sequence of zeroes with no previous
        // match.
        static bool extra_matches(
                std::span<stream_t const> data, size_t const base_node,
                size_t const ubound, size_t const lbound,
                std::vector<AdjListNode<SaxmanAdaptor>>& matches) noexcept {
            using Match_t = AdjListNode<SaxmanAdaptor>::MatchInfo;
            ignore_unused_variable_warning(lbound);
            // Can't encode zero match after this point.
            if (base_node >= SearchBufSize - 1) {
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
                            base_node, Match_t{numeric_limits<size_t>::max(), length},
                            EdgeType::zerofill);
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
    static void decode(istream& input, iostream& Dest, size_t const Size) {
        using SaxIStream = LZSSIStream<SaxmanAdaptor>;
        using diff_t     = std::make_signed_t<size_t>;

        SaxIStream source(input);
        auto const size = static_cast<std::make_signed_t<size_t>>(Size);

        constexpr auto const buffer_size
                = static_cast<diff_t>(SaxmanAdaptor::SearchBufSize);

        // Loop while the file is good and we haven't gone over the declared
        // length.
        while (input.good() && input.tellg() < size) {
            if (source.descriptor_bit() != 0U) {
                // Symbolwise match.
                if (input.peek() == EOF) {
                    break;
                }
                Write1(Dest, source.get_byte());
            } else {
                if (input.peek() == EOF) {
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
                auto const base = Dest.tellp();
                auto const offset
                        = ((base_offset - base) % buffer_size) + base - buffer_size;

                if (offset < base) {
                    // If the offset is before the current output position, we
                    // copy bytes from the given location.
                    for (auto csrc = offset; csrc < offset + length; csrc++) {
                        auto const Pointer = Dest.tellp();
                        Dest.seekg(csrc);
                        uint8_t const Byte = Read1(Dest);
                        Dest.seekp(Pointer);
                        Write1(Dest, Byte);
                    }
                } else {
                    // Otherwise, it is a zero fill.
                    fill_n(ostreambuf_iterator<char>(Dest), length, 0);
                }
            }
        }
    }

    static void encode(ostream& Dest, uint8_t const*& Data, size_t const Size) {
        using EdgeType   = typename SaxmanAdaptor::EdgeType;
        using SaxOStream = LZSSOStream<SaxmanAdaptor>;

        // Compute optimal Saxman parsing of input file.
        auto       list = find_optimal_lzss_parse(Data, Size, SaxmanAdaptor{});
        SaxOStream output(Dest);

        // Go through each edge in the optimal path.
        for (auto const& edge : list.parse_list) {
            switch (edge.get_type()) {
            case EdgeType::symbolwise:
                output.descriptor_bit(1);
                output.put_byte(edge.get_symbol());
                break;
            case EdgeType::dictionary:
            case EdgeType::zerofill: {
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
            case EdgeType::terminator:
                break;
            case EdgeType::invalid:
                // This should be unreachable.
                std::cerr << "Compression produced invalid edge type "
                          << static_cast<size_t>(edge.get_type()) << std::endl;
                __builtin_unreachable();
            }
        }
    }
};

bool saxman::decode(istream& Source, iostream& Dest, size_t Size) {
    if (Size == 0) {
        Size = LittleEndian::Read2(Source);
    }

    auto const   Location = Source.tellg();
    stringstream input(ios::in | ios::out | ios::binary);
    extract(Source, input);

    saxman_internal::decode(input, Dest, Size);
    Source.seekg(Location + input.tellg());
    return true;
}

bool saxman::encode(
        ostream& Dest, uint8_t const* data, size_t const Size, bool const WithSize) {
    stringstream outbuff(ios::in | ios::out | ios::binary);
    auto const   Start = outbuff.tellg();
    saxman_internal::encode(outbuff, data, Size);
    if (WithSize) {
        outbuff.seekg(Start);
        outbuff.ignore(numeric_limits<streamsize>::max());
        auto FullSize = outbuff.gcount();
        LittleEndian::Write2(Dest, static_cast<uint16_t>(FullSize));
    }
    outbuff.seekg(Start);
    Dest << outbuff.rdbuf();
    return true;
}
