/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * Copyright (C) Clownacy 2016
 * Copyright (C) Flamewing 2016 <flamewing.sonic@gmail.com>
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
#include <mdcomp/lzss.hh>
#include <mdcomp/rocket.hh>

#include <cstdint>
#include <iostream>
#include <istream>
#include <ostream>
#include <sstream>
#include <type_traits>


using std::array;
using std::fill_n;
using std::ios;
using std::iostream;
using std::istream;
using std::make_signed_t;
using std::numeric_limits;
using std::ostream;
using std::ostreambuf_iterator;
using std::streamsize;
using std::stringstream;

template <>
size_t moduled_rocket::PadMaskBits = 1U;

struct rocket_internal {
    // NOTE: This has to be changed for other LZSS-based compression schemes.
    struct RocketAdaptor {
        using stream_t            = uint8_t;
        using stream_endian_t     = BigEndian;
        using descriptor_t        = uint8_t;
        using descriptor_endian_t = LittleEndian;
        using SlidingWindow_t     = SlidingWindow<RocketAdaptor>;
        enum class EdgeType : size_t { invalid, symbolwise, dictionary };
        // Number of bits on descriptor bitfield.
        constexpr static size_t const NumDescBits = sizeof(descriptor_t) * 8;
        // Number of bits used in descriptor bitfield to signal the end-of-file
        // marker sequence.
        constexpr static size_t const NumTermBits = 0;
        // Number of bits for end-of-file marker.
        constexpr static size_t const TerminatorWeight = 0;
        // Flag that tells the compressor that new descriptor fields is needed
        // when a new bit is needed and all bits in the previous one have been
        // used up.
        constexpr static bool const NeedEarlyDescriptor = false;
        // Flag that marks the descriptor bits as being in big-endian bit
        // order (that is, highest bits come out first).
        constexpr static bool const DescriptorLittleEndianBits = true;
        // How many characters to skip looking for matchs for at the start.
        constexpr static size_t const FirstMatchPosition = 0x3C0;
        // Size of the search buffer.
        constexpr static size_t const SearchBufSize = 0x400;
        // Size of the look-ahead buffer.
        constexpr static size_t const LookAheadBufSize = 0x40;
        // Total size of the sliding window.
        constexpr static size_t const SlidingWindowSize
                = SearchBufSize + LookAheadBufSize;
        // Creates the (multilayer) sliding window structure.
        static auto create_sliding_window(
                stream_t const* dt, size_t const size) noexcept {
            return array<SlidingWindow_t, 1>{SlidingWindow_t{
                    dt, size, SearchBufSize, 2, LookAheadBufSize,
                    EdgeType::dictionary}};
        }
        // Given an edge type, computes how many bits are used in the descriptor
        // field.
        constexpr static size_t desc_bits(EdgeType const type) noexcept {
            // Rocket always uses a single bit descriptor.
            ignore_unused_variable_warning(type);
            return 1;
        }
        // Given an edge type, computes how many bits are used in total by this
        // edge. A return of "numeric_limits<size_t>::max()" means "infinite",
        // or "no edge".
        constexpr static size_t
                edge_weight(EdgeType const type, size_t length) noexcept {
            ignore_unused_variable_warning(length);
            switch (type) {
            case EdgeType::symbolwise:
                // 8-bit value.
                return desc_bits(type) + 8;
            case EdgeType::dictionary:
                // 10-bit distance, 6-bit length.
                return desc_bits(type) + 10 + 6;
            case EdgeType::invalid:
                return numeric_limits<size_t>::max();
            }
            __builtin_unreachable();
        }
        // Rocket finds no additional matches over normal LZSS.
        static bool extra_matches(
                stream_t const* data, size_t const basenode,
                size_t const ubound, size_t const lbound,
                LZSSGraph<RocketAdaptor>::MatchVector& matches) noexcept {
            ignore_unused_variable_warning(
                    data, basenode, ubound, lbound, matches);
            // Do normal matches.
            return false;
        }
        // Rocket needs no additional padding at the end-of-file.
        constexpr static size_t get_padding(size_t const totallen) noexcept {
            ignore_unused_variable_warning(totallen);
            return 0;
        }
    };

public:
    static void decode(istream& in, iostream& Dst) {
        using RockIStream = LZSSIStream<RocketAdaptor>;
        using diff_t      = make_signed_t<size_t>;

        in.ignore(2);
        size_t const Size = BigEndian::Read2(in) + 4;
        RockIStream  src(in);

        while (in.good() && size_t(in.tellg()) < Size) {
            if (src.descbit() != 0U) {
                // Symbolwise match.
                uint8_t const Byte = Read1(in);
                Write1(Dst, Byte);
            } else {
                // Dictionary match.
                // Distance and length of match.
                diff_t const high   = src.getbyte();
                diff_t const low    = src.getbyte();
                diff_t       length = ((high & 0xFC) >> 2) + 1U;
                diff_t       offset = ((high & 3) << 8) | low;
                // The offset is stored as being absolute within a 0x400-byte
                // buffer, starting at position 0x3C0. We just rebase it around
                // basedest + 0x3C0u.
                constexpr auto const bias
                        = diff_t(RocketAdaptor::FirstMatchPosition);
                diff_t const basedest = diff_t(Dst.tellp());
                offset                = diff_t(
                        ((offset - basedest - bias)
                         % RocketAdaptor::SearchBufSize)
                        + basedest - RocketAdaptor::SearchBufSize);

                if (offset < 0) {
                    diff_t cnt = offset + length < 0
                                         ? length
                                         : length - (offset + length);
                    fill_n(ostreambuf_iterator<char>(Dst), cnt, 0x20);
                    length -= cnt;
                    offset += cnt;
                }
                for (diff_t csrc = offset; csrc < offset + length; csrc++) {
                    diff_t const Pointer = diff_t(Dst.tellp());
                    Dst.seekg(csrc);
                    uint8_t const Byte = Read1(Dst);
                    Dst.seekp(Pointer);
                    Write1(Dst, Byte);
                }
            }
        }
    }

    static void encode(ostream& Dst, uint8_t const*& Data, size_t const Size) {
        using EdgeType    = typename RocketAdaptor::EdgeType;
        using RockGraph   = LZSSGraph<RocketAdaptor>;
        using RockOStream = LZSSOStream<RocketAdaptor>;

        // Compute optimal Rocket parsing of input file.
        RockGraph          enc(Data, Size);
        RockGraph::AdjList list = enc.find_optimal_parse();
        RockOStream        out(Dst);

        // Go through each edge in the optimal path.
        for (auto const& edge : list) {
            switch (edge.get_type()) {
            case EdgeType::symbolwise:
                out.descbit(1);
                out.putbyte(edge.get_symbol());
                break;
            case EdgeType::dictionary: {
                size_t const len  = edge.get_length();
                size_t const dist = edge.get_distance();
                size_t const pos  = (edge.get_pos() - dist)
                                   % RocketAdaptor::SearchBufSize;
                out.descbit(0);
                out.putbyte(((len - 1) << 2) | (pos >> 8));
                out.putbyte(pos);
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

bool rocket::decode(istream& Src, iostream& Dst) {
    size_t const Location = Src.tellg();
    stringstream in(ios::in | ios::out | ios::binary);
    extract(Src, in);

    rocket_internal::decode(in, Dst);
    Src.seekg(Location + in.tellg());
    return true;
}

bool rocket::encode(istream& Src, ostream& Dst) {
    // We will pre-fill the buffer with 0x3C0 0x20's.
    stringstream in(ios::in | ios::out | ios::binary);
    fill_n(ostreambuf_iterator<char>(in),
           rocket_internal::RocketAdaptor::FirstMatchPosition, 0x20);
    // Copy to buffer.
    in << Src.rdbuf();
    in.seekg(0);
    return basic_rocket::encode(in, Dst);
}

bool rocket::encode(ostream& Dst, uint8_t const* data, size_t const Size) {
    // Internal buffer.
    stringstream outbuff(ios::in | ios::out | ios::binary);
    rocket_internal::encode(outbuff, data, Size);

    // Fill in header
    // Size of decompressed file
    BigEndian::Write2(
            Dst, Size - rocket_internal::RocketAdaptor::FirstMatchPosition);
    // Size of compressed file
    BigEndian::Write2(Dst, outbuff.tellp());

    outbuff.seekg(0);
    Dst << outbuff.rdbuf();
    return true;
}
