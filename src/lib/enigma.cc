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

#include "mdcomp/enigma.hh"

#include "mdcomp/bigendian_io.hh"
#include "mdcomp/bitstream.hh"
#include "mdcomp/ignore_unused_variable_warning.hh"

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

using std::array;
using std::ios;
using std::istream;
using std::make_index_sequence;
using std::map;
using std::ostream;
using std::pair;
using std::set;
using std::stringstream;
using std::vector;

using eni_ibitstream = ibitstream<uint16_t, bit_endian::big, big_endian, true>;
using eni_obitstream = obitstream<uint16_t, bit_endian::big, big_endian>;

template <typename Callback>
class base_flag_io {
public:
    using callback_t = Callback;

    struct tag {};

    static base_flag_io const& get(size_t flags);

    constexpr explicit base_flag_io(callback_t callback_in) noexcept
            : callback(callback_in) {}

    template <typename... Ts>
    auto operator()(Ts&&... args) const {
        return this->callback(std::forward<Ts>(args)...);
    }

private:
    callback_t* callback;
};

using flag_reader = base_flag_io<uint16_t(eni_ibitstream&)>;
using flag_writer = base_flag_io<void(eni_obitstream&, uint16_t)>;

template <size_t N>
uint16_t read_bitfield(eni_ibitstream& bits) {
    auto const read_bit_flag
            = [&]<size_t I>(std::integral_constant<size_t, I>) -> uint32_t {
        if constexpr ((N & (1U << (I - 1))) != 0) {
            return static_cast<uint32_t>(bits.pop() << (I + 10U));
        } else {
            return 0U;
        }
    };
    auto const read_bit_flags = [&]<size_t... Is>(std::index_sequence<Is...>) {
        constexpr size_t const count = sizeof...(Is);
        return uint16_t(
                ((read_bit_flag(std::integral_constant<size_t, count - Is>{})) | ...));
    };
    return read_bit_flags(make_index_sequence<5>());
}

template <size_t N>
void write_bitfield(eni_obitstream& bits, uint16_t const flags) {
    auto const write_bit_flag = [&]<size_t I>(std::integral_constant<size_t, I>) {
        if constexpr ((N & (1U << (I - 1))) != 0) {
            bits.push(static_cast<uint16_t>((flags & (1U << (I + 10U))) != 0));
        }
    };
    auto const write_bit_flags = [&]<size_t... Is>(std::index_sequence<Is...>) {
        constexpr size_t const count = sizeof...(Is);
        ((write_bit_flag(std::integral_constant<size_t, count - Is>{})), ...);
    };
    write_bit_flags(make_index_sequence<5>());
}

template <std::size_t... I>
constexpr auto create_mask_array(flag_reader::tag, std::index_sequence<I...>) {
    return array<flag_reader, sizeof...(I)>{flag_reader(read_bitfield<I>)...};
}

template <std::size_t... I>
constexpr auto create_mask_array(flag_writer::tag, std::index_sequence<I...>) {
    return array<flag_writer, sizeof...(I)>{flag_writer(write_bitfield<I>)...};
}

template <typename Callback>
base_flag_io<Callback> const& base_flag_io<Callback>::get(size_t const flags) {
    constexpr static auto const array
            = create_mask_array(tag{}, make_index_sequence<32>());
    return array[flags];
}

// Comparison functor, see below.
struct compare_count {
    bool operator()(
            pair<uint16_t const, size_t>& left, pair<uint16_t const, size_t>& right) {
        return (left.second < right.second);
    }
};

// This flushes (if needed) the contents of the inlined data buffer.
static inline void flush_buffer(
        vector<uint16_t>& buffer, eni_obitstream& bits, flag_writer& put_mask,
        size_t const packet_length) {
    if (buffer.empty()) {
        return;
    }

    bits.write(0x70U | ((buffer.size() - 1) & 0xfU), 7);
    for (auto const value : buffer) {
        put_mask(bits, value);
        bits.write(value & 0x7ffU, packet_length);
    }
    buffer.clear();
}

template <>
size_t moduled_enigma::pad_mask_bits = 1U;

class enigma_internal {
public:
    static void decode(std::istream& input, std::ostream& dest) {
        // Read header.
        size_t const   packet_length      = read1(input);
        auto           get_mask           = flag_reader::get(read1(input));
        uint16_t       incrementing_value = big_endian::read2(input);
        uint16_t const common_value       = big_endian::read2(input);

        eni_ibitstream bits(input);

        constexpr static std::array<int, 3> const mode_delta_lut = {0, 1, -1};

        // Lets put in a safe termination condition here.
        while (input.good()) {
            if (bits.pop() != 0U) {
                size_t const mode = bits.read(2);
                switch (mode) {
                case 2:
                case 1:
                case 0: {
                    size_t const   count = bits.read(4) + 1;
                    uint16_t const flags = get_mask(bits);
                    uint16_t       outv  = bits.read(packet_length);
                    outv |= flags;

                    for (size_t i = 0; i < count; i++) {
                        big_endian::write2(dest, outv);
                        outv += mode_delta_lut[mode];
                    }
                    break;
                }
                case 3: {
                    size_t const count = bits.read(4);
                    // This marks decompression as being done.
                    if (count == 0x0F) {
                        return;
                    }

                    for (size_t i = 0; i <= count; i++) {
                        uint16_t const flags = get_mask(bits);
                        uint16_t const outv  = bits.read(packet_length);
                        big_endian::write2(dest, outv | flags);
                    }
                    break;
                }
                default:
                    __builtin_unreachable();
                }
            } else {
                if (bits.pop() == 0U) {
                    size_t const count = bits.read(4) + 1;
                    for (size_t i = 0; i < count; i++) {
                        big_endian::write2(dest, incrementing_value++);
                    }
                } else {
                    size_t const count = bits.read(4) + 1;
                    for (size_t i = 0; i < count; i++) {
                        big_endian::write2(dest, common_value);
                    }
                }
            }
        }
    }

    static void encode(std::istream& source, std::ostream& dest) {
        // To unpack source into 2-byte words.
        vector<uint16_t> unpack;
        // Frequency map.
        map<uint16_t, size_t> counts;
        // Presence map.
        set<uint16_t> elems;

        // Unpack source into array. Along the way, build frequency and presence
        // maps.
        uint16_t mask_val = 0;
        source.clear();
        source.seekg(0);
        while (true) {
            uint16_t const value = big_endian::read2(source);
            if (!source.good()) {
                break;
            }
            mask_val |= value;
            counts[value] += 1;
            elems.insert(value);
            unpack.push_back(value);
        }

        auto         put_mask = flag_writer::get(mask_val >> 11U);
        size_t const packet_length
                = std::bit_cast<unsigned>(std::bit_width(mask_val & 0x7ffU));

        // Find the most common 2-byte value.
        compare_count const comp;
        auto const          high = max_element(counts.begin(), counts.end(), comp);
        uint16_t const      common_value = high->first;
        // No longer needed.
        counts.clear();

        // Find incrementing (not necessarily contiguous) runs.
        // The original algorithm does this for all 65536 2-byte words, while
        // this version only checks the 2-byte words actually in the file.
        map<uint16_t, size_t> runs;
        for (auto next : elems) {
            auto [value, inserted] = runs.emplace(next, 0);
            for (auto& elem : unpack) {
                if (elem == next) {
                    next++;
                    value->second += 1;
                }
            }
        }
        // No longer needed.
        elems.clear();

        // Find the starting 2-byte value with the longest incrementing run.
        auto     incr               = max_element(runs.begin(), runs.end(), comp);
        uint16_t incrementing_value = incr->first;
        // No longer needed.
        runs.clear();

        // Output header.
        write1(dest, packet_length & std::numeric_limits<uint8_t>::max());
        write1(dest, mask_val >> 11U);
        big_endian::write2(dest, incrementing_value);
        big_endian::write2(dest, common_value);

        // Time now to compress the file.
        eni_obitstream   bits(dest);
        vector<uint16_t> buffer;
        size_t           position = 0;
        while (position < unpack.size()) {
            uint16_t const value = unpack[position];
            if (value == incrementing_value) {
                flush_buffer(buffer, bits, put_mask, packet_length);
                uint16_t next  = value + 1;
                size_t   count = 0;
                for (size_t i = position + 1; i < unpack.size() && count < 0xf; i++) {
                    if (next != unpack[i]) {
                        break;
                    }
                    next++;
                    count++;
                }
                bits.write(0b00'0000U | (count & 0xffU), 6);
                incrementing_value = next;
                position += count;
            } else if (value == common_value) {
                flush_buffer(buffer, bits, put_mask, packet_length);
                uint16_t const next  = value;
                size_t         count = 0;
                for (size_t i = position + 1; i < unpack.size() && count < 0xf; i++) {
                    if (next != unpack[i]) {
                        break;
                    }
                    count++;
                }
                bits.write(0b01'0000U | (count & 0xffU), 6);
                position += count;
            } else {
                uint16_t next  = unpack[position + 1];
                uint16_t delta = next - value;

                constexpr const uint16_t minus_one = std::numeric_limits<uint16_t>::max();
                if (position + 1 < unpack.size() && next != incrementing_value
                    && (delta == minus_one || delta == 0 || delta == 1)) {
                    flush_buffer(buffer, bits, put_mask, packet_length);
                    size_t count = 1;
                    next += delta;
                    for (size_t i = position + 2; i < unpack.size() && count < 0xf; i++) {
                        if (next != unpack[i] || next == incrementing_value) {
                            break;
                        }
                        next += delta;
                        count++;
                    }

                    if (delta == minus_one) {
                        delta = 2;
                    }

                    delta = (((delta | 4U)) << 4U) & 0xffffU;
                    bits.write((delta | count) & 0xffU, 7);
                    put_mask(bits, value);
                    bits.write(value & 0x7ffU, packet_length);
                    position += count;
                } else {
                    if (buffer.size() >= 0xf) {
                        flush_buffer(buffer, bits, put_mask, packet_length);
                    }

                    buffer.push_back(value);
                }
            }
            position++;
        }

        flush_buffer(buffer, bits, put_mask, packet_length);

        // Terminator.
        bits.write(0x7f, 7);
        bits.flush();
    }
};

bool enigma::decode(istream& source, ostream& dest) {
    auto const   location = source.tellg();
    stringstream input(ios::in | ios::out | ios::binary);
    extract(source, input);

    enigma_internal::decode(input, dest);
    source.seekg(location + input.tellg());
    return true;
}

bool enigma::encode(istream& source, ostream& dest) {
    enigma_internal::encode(source, dest);
    return true;
}

bool enigma::encode(std::ostream& dest, std::span<uint8_t const> data) {
    stringstream source(ios::in | ios::out | ios::binary);
    source.write(reinterpret_cast<char const*>(data.data()), std::ssize(data));
    source.seekg(0);
    return encode(source, dest);
}
