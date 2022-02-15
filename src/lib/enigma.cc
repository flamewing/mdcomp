/*
 * Copyright (C) Flamewing 2011-2016 <flamewing.sonic@gmail.com>
 * Copyright (C) 2002-2004 The KENS Project Development Team
 * Copyright (C) 2002-2003 Roger Sanders (AKA Nemesis)
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

#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <istream>
#include <limits>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#    include <intrin.h>
#    pragma intrinsic(_BitScanReverse)
#endif

#include <mdcomp/bigendian_io.hh>
#include <mdcomp/bitstream.hh>
#include <mdcomp/enigma.hh>
#include <mdcomp/ignore_unused_variable_warning.hh>

using std::array;
using std::forward;
using std::ios;
using std::iostream;
using std::istream;
using std::make_index_sequence;
using std::make_signed_t;
using std::map;
using std::ostream;
using std::pair;
using std::set;
using std::streamsize;
using std::stringstream;
using std::vector;

using EniIBitstream = ibitstream<uint16_t, bit_endian::big, BigEndian, true>;
using EniOBitstream = obitstream<uint16_t, bit_endian::big, BigEndian>;

template <typename Callback>
class base_flag_io {
public:
    using Callback_t = Callback;
    struct tag {};
    static const base_flag_io& get(size_t n);
    constexpr explicit base_flag_io(Callback_t callback_) noexcept
            : callback(callback_) {}
    template <typename... Ts>
    auto operator()(Ts&&... args) const {
        return this->callback(forward<Ts>(args)...);
    }

private:
    Callback_t* callback;
};

using flag_reader = base_flag_io<uint16_t(EniIBitstream&)>;
using flag_writer = base_flag_io<void(EniOBitstream&, uint16_t)>;

template <size_t N>
uint16_t read_bitfield(EniIBitstream& bits) {
    const auto read_bit_flag
            = [&]<size_t I>(std::integral_constant<size_t, I>) -> uint32_t {
        if constexpr ((N & (1U << (I - 1))) != 0) {
            return uint32_t(bits.pop() << (I + 10U));
        } else {
            return 0U;
        }
    };
    const auto read_bit_flags = [&]<size_t... Is>(std::index_sequence<Is...>) {
        constexpr const size_t count = sizeof...(Is);
        return uint16_t(
                ((read_bit_flag(std::integral_constant<size_t, count - Is>{})) | ...));
    };
    return read_bit_flags(make_index_sequence<5>());
}

template <size_t N>
void write_bitfield(EniOBitstream& bits, uint16_t const flags) {
    const auto write_bit_flag = [&]<size_t I>(std::integral_constant<size_t, I>) {
        if constexpr ((N & (1U << (I - 1))) != 0) {
            bits.push(static_cast<uint16_t>((flags & (1U << (I + 10U))) != 0));
        }
    };
    const auto write_bit_flags = [&]<size_t... Is>(std::index_sequence<Is...>) {
        constexpr const size_t count = sizeof...(Is);
        ((write_bit_flag(std::integral_constant<size_t, count - Is>{})), ...);
    };
    write_bit_flags(make_index_sequence<5>());
}

template <std::size_t... I>
constexpr auto createMaskArray(flag_reader::tag, std::index_sequence<I...>) {
    return array<flag_reader, sizeof...(I)>{flag_reader(read_bitfield<I>)...};
}

template <std::size_t... I>
constexpr auto createMaskArray(flag_writer::tag, std::index_sequence<I...>) {
    return array<flag_writer, sizeof...(I)>{flag_writer(write_bitfield<I>)...};
}

template <typename Callback>
const base_flag_io<Callback>& base_flag_io<Callback>::get(size_t const n) {
    constexpr static const auto Array = createMaskArray(tag{}, make_index_sequence<32>());
    return Array[n];
}

// Comparison functor, see below.
struct Compare_count {
    bool operator()(
            pair<uint16_t const, size_t>& it1, pair<uint16_t const, size_t>& it2) {
        return (it1.second < it2.second);
    }
};

// This flushes (if needed) the contents of the inlined data buffer.
static inline void flush_buffer(
        vector<uint16_t>& buf, EniOBitstream& bits, flag_writer& putMask,
        uint16_t const packet_length) {
    if (buf.empty()) {
        return;
    }

    bits.write(0x70U | ((buf.size() - 1) & 0xfU), 7);
    for (const auto v : buf) {
        putMask(bits, v);
        bits.write(v & 0x7ffU, packet_length);
    }
    buf.clear();
}

template <>
size_t moduled_enigma::PadMaskBits = 1U;

class enigma_internal {
public:
    static void decode(std::istream& in, std::ostream& Dst) {
        // Read header.
        size_t const packet_length      = Read1(in);
        auto         getMask            = flag_reader::get(Read1(in));
        size_t       incrementing_value = BigEndian::Read2(in);
        size_t const common_value       = BigEndian::Read2(in);

        EniIBitstream bits(in);

        constexpr static std::array<int, 3> const modeDeltaLUT = {0, 1, -1};

        // Lets put in a safe termination condition here.
        while (in.good()) {
            if (bits.pop() != 0U) {
                size_t const mode = bits.read(2);
                switch (mode) {
                case 2:
                case 1:
                case 0: {
                    size_t const   cnt   = bits.read(4) + 1;
                    uint16_t const flags = getMask(bits);
                    uint16_t       outv  = bits.read(packet_length);
                    outv |= flags;

                    for (size_t i = 0; i < cnt; i++) {
                        BigEndian::Write2(Dst, outv);
                        outv += modeDeltaLUT[mode];
                    }
                    break;
                }
                case 3: {
                    size_t const cnt = bits.read(4);
                    // This marks decompression as being done.
                    if (cnt == 0x0F) {
                        return;
                    }

                    for (size_t i = 0; i <= cnt; i++) {
                        uint16_t flags = getMask(bits);
                        uint16_t outv  = bits.read(packet_length);
                        BigEndian::Write2(Dst, outv | flags);
                    }
                    break;
                }
                default:
                    __builtin_unreachable();
                }
            } else {
                if (bits.pop() == 0U) {
                    size_t const cnt = bits.read(4) + 1;
                    for (size_t i = 0; i < cnt; i++) {
                        BigEndian::Write2(Dst, incrementing_value++);
                    }
                } else {
                    size_t const cnt = bits.read(4) + 1;
                    for (size_t i = 0; i < cnt; i++) {
                        BigEndian::Write2(Dst, common_value);
                    }
                }
            }
        }
    }

    static void encode(std::istream& Src, std::ostream& Dst) {
        // To unpack source into 2-byte words.
        vector<uint16_t> unpack;
        // Frequency map.
        map<uint16_t, size_t> counts;
        // Presence map.
        set<uint16_t> elems;

        // Unpack source into array. Along the way, build frequency and presence
        // maps.
        uint16_t maskval = 0;
        Src.clear();
        Src.seekg(0);
        while (true) {
            uint16_t v = BigEndian::Read2(Src);
            if (!Src.good()) {
                break;
            }
            maskval |= v;
            counts[v] += 1;
            elems.insert(v);
            unpack.push_back(v);
        }

        auto           putMask       = flag_writer::get(maskval >> 11U);
        uint16_t const packet_length = std::bit_width(maskval & 0x7ffU);

        // Find the most common 2-byte value.
        Compare_count  cmp;
        auto           high         = max_element(counts.begin(), counts.end(), cmp);
        uint16_t const common_value = high->first;
        // No longer needed.
        counts.clear();

        // Find incrementing (not necessarily contiguous) runs.
        // The original algorithm does this for all 65536 2-byte words, while
        // this version only checks the 2-byte words actually in the file.
        map<uint16_t, size_t> runs;
        for (auto next : elems) {
            auto val = runs.emplace(next, 0).first;
            for (auto& elem : unpack) {
                if (elem == next) {
                    next++;
                    val->second += 1;
                }
            }
        }
        // No longer needed.
        elems.clear();

        // Find the starting 2-byte value with the longest incrementing run.
        auto     incr               = max_element(runs.begin(), runs.end(), cmp);
        uint16_t incrementing_value = incr->first;
        // No longer needed.
        runs.clear();

        // Output header.
        Write1(Dst, packet_length);
        Write1(Dst, maskval >> 11U);
        BigEndian::Write2(Dst, incrementing_value);
        BigEndian::Write2(Dst, common_value);

        // Time now to compress the file.
        EniOBitstream    bits(Dst);
        vector<uint16_t> buf;
        size_t           pos = 0;
        while (pos < unpack.size()) {
            uint16_t const v = unpack[pos];
            if (v == incrementing_value) {
                flush_buffer(buf, bits, putMask, packet_length);
                uint16_t next = v + 1;
                size_t   cnt  = 0;
                for (size_t i = pos + 1; i < unpack.size() && cnt < 0xf; i++) {
                    if (next != unpack[i]) {
                        break;
                    }
                    next++;
                    cnt++;
                }
                bits.write(0x00U | cnt, 6);
                incrementing_value = next;
                pos += cnt;
            } else if (v == common_value) {
                flush_buffer(buf, bits, putMask, packet_length);
                uint16_t next = v;
                size_t   cnt  = 0;
                for (size_t i = pos + 1; i < unpack.size() && cnt < 0xf; i++) {
                    if (next != unpack[i]) {
                        break;
                    }
                    cnt++;
                }
                bits.write(0x10U | cnt, 6);
                pos += cnt;
            } else {
                uint16_t next  = unpack[pos + 1];
                uint16_t delta = next - v;

                constexpr const uint16_t minus_one = std::numeric_limits<uint16_t>::max();
                if (pos + 1 < unpack.size() && next != incrementing_value
                    && (delta == minus_one || delta == 0 || delta == 1)) {
                    flush_buffer(buf, bits, putMask, packet_length);
                    size_t cnt = 1;
                    next += delta;
                    for (size_t i = pos + 2; i < unpack.size() && cnt < 0xf; i++) {
                        if (next != unpack[i] || next == incrementing_value) {
                            break;
                        }
                        next += delta;
                        cnt++;
                    }

                    if (delta == minus_one) {
                        delta = 2;
                    }

                    delta = ((delta | 4U) << 4U);
                    bits.write(delta | cnt, 7);
                    putMask(bits, v);
                    bits.write(v & 0x7ffU, packet_length);
                    pos += cnt;
                } else {
                    if (buf.size() >= 0xf) {
                        flush_buffer(buf, bits, putMask, packet_length);
                    }

                    buf.push_back(v);
                }
            }
            pos++;
        }

        flush_buffer(buf, bits, putMask, packet_length);

        // Terminator.
        bits.write(0x7f, 7);
        bits.flush();
    }
};

bool enigma::decode(istream& Src, ostream& Dst) {
    size_t const Location = Src.tellg();
    stringstream in(ios::in | ios::out | ios::binary);
    extract(Src, in);

    enigma_internal::decode(in, Dst);
    Src.seekg(Location + in.tellg());
    return true;
}

bool enigma::encode(istream& Src, ostream& Dst) {
    enigma_internal::encode(Src, Dst);
    return true;
}

bool enigma::encode(std::ostream& Dst, uint8_t const* data, size_t const Size) {
    stringstream Src(ios::in | ios::out | ios::binary);
    Src.write(reinterpret_cast<char const*>(data), Size);
    Src.seekg(0);
    return encode(Src, Dst);
}
