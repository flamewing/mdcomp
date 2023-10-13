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

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <istream>
#include <limits>
#include <map>
#include <numeric>
#include <ostream>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

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
    return read_bit_flags(std::make_index_sequence<5>());
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
    write_bit_flags(std::make_index_sequence<5>());
}

template <std::size_t... I>
constexpr auto create_mask_array(flag_reader::tag, std::index_sequence<I...>) {
    return std::array{flag_reader(read_bitfield<I>)...};
}

template <std::size_t... I>
constexpr auto create_mask_array(flag_writer::tag, std::index_sequence<I...>) {
    return std::array{flag_writer(write_bitfield<I>)...};
}

template <typename Callback>
base_flag_io<Callback> const& base_flag_io<Callback>::get(size_t const flags) {
    constexpr static auto const array
            = create_mask_array(tag{}, std::make_index_sequence<32>());
    return array[flags];
}

template <>
size_t moduled_enigma::pad_mask_bits = 1U;

using enigma_output_iterator = endian_output_iterator<big_endian, uint16_t>;

static_assert(std::output_iterator<enigma_output_iterator, uint16_t>);

class enigma_internal {
public:
    static void decode(std::istream& input, std::ostream& dest) {
        // Read header.
        size_t const   packet_length      = read1(input);
        auto           read_mask          = flag_reader::get(read1(input));
        uint16_t       incrementing_value = big_endian::read2(input);
        uint16_t const common_value       = big_endian::read2(input);

        eni_ibitstream bits(input);

        auto const read_count = [&]() noexcept -> ptrdiff_t {
            return bits.read(4) + 1;
        };

        auto const read_value = [&]() noexcept -> uint16_t {
            uint16_t const flags = read_mask(bits);
            uint16_t const outv  = bits.read(packet_length);
            return outv | flags;
        };

        auto const make_generator = [&](int delta) noexcept {
            return [delta, value = read_value()]() mutable noexcept {
                uint16_t current = value;
                value += delta;
                return current;
            };
        };

        auto const incrementor = [&incrementing_value]() noexcept {
            return incrementing_value++;
        };

        enigma_output_iterator output(dest);

        // Lets put in a safe termination condition here.
        while (input.good()) {
            if (bits.pop() != 0U) {
                auto const mode  = bits.read(2);
                auto const count = read_count();
                switch (mode) {
                case 0:
                    std::ranges::fill_n(output, count, read_value());
                    break;
                case 1:
                    std::ranges::generate_n(output, count, make_generator(1));
                    break;
                case 2:
                    std::ranges::generate_n(output, count, make_generator(-1));
                    break;
                case 3: {
                    // This marks decompression as being done.
                    if (count == 0x10) {
                        return;
                    }
                    std::ranges::generate_n(output, count, read_value);
                    break;
                }
                default:
                    __builtin_unreachable();
                }
            } else {
                auto const mode  = bits.pop();
                auto const count = read_count();
                if (mode == 0U) {
                    std::ranges::generate_n(output, count, incrementor);
                } else {
                    std::ranges::fill_n(output, count, common_value);
                }
            }
        }
    }

    static void encode(std::istream& source, std::ostream& dest) {
        using diff_t = std::make_signed_t<size_t>;
        // To unpack source into 2-byte words.
        std::vector<uint16_t> unpack;
        // Frequency map.
        std::map<uint16_t, diff_t> counts;
        // Presence map.
        std::set<uint16_t> unique_elems;

        // Unpack source into array. Along the way, build frequency and presence
        // maps.
        uint16_t mask_val = 0;
        source.seekg(0);
        source.ignore(std::numeric_limits<std::streamsize>::max());
        auto const full_size = source.gcount() / 2;
        source.seekg(0);
        for (diff_t loc = 0; loc < full_size; loc++) {
            uint16_t const value = big_endian::read2(source);
            mask_val |= value;
            counts[value] += 1;
            unique_elems.insert(value);
            unpack.push_back(value);
        }

        auto         put_mask = flag_writer::get(mask_val >> 11U);
        size_t const packet_length
                = std::bit_cast<unsigned>(std::bit_width(mask_val & 0x7ffU));

        // Find the most common 2-byte value.
        auto const high = std::ranges::max_element(
                counts, {}, &decltype(counts)::value_type::second);
        uint16_t const common_value = high->first;
        // No longer needed.
        counts.clear();

        // Find incrementing (not necessarily contiguous) runs.
        // The original algorithm does this for all 65536 2-byte words, while
        // this version only checks the 2-byte words actually in the file.
        using kv_pair     = std::pair<uint16_t, diff_t>;
        auto transform_fn = [&](uint16_t elem) noexcept -> kv_pair {
            auto const predicate = [next = elem](uint16_t value) mutable noexcept {
                const bool result = value == next;
                next += static_cast<uint16_t>(result);
                return result;
            };
            return {elem, std::ranges::count_if(unpack, predicate)};
        };
        auto reduce_fn = [](auto const& left, auto const& right) {
            return std::ranges::max(left, right, {}, &kv_pair::second);
        };
        // Find the starting 2-byte value with the longest incrementing run.
        // I'd love to use std::views::transform, but there is no ranges version
        // of std::reduce. Composing std::reduce with std::views::transform
        // generates the same assembly as std::transform_reduce, so this is
        // fine because it needs one less temporary.
        uint16_t incrementing_value = std::transform_reduce(
                                              unique_elems.cbegin(), unique_elems.cend(),
                                              kv_pair{}, reduce_fn, transform_fn)
                                              .first;

        // Output header.
        write1(dest, packet_length & std::numeric_limits<uint8_t>::max());
        write1(dest, mask_val >> 11U);
        big_endian::write2(dest, incrementing_value);
        big_endian::write2(dest, common_value);

        // Time now to compress the file.
        eni_obitstream        bits(dest);
        std::vector<uint16_t> buffer;

        // Writes a mask and a value to the output stream.
        auto const write_value = [&](uint16_t const value) {
            put_mask(bits, value);
            bits.write(value & 0x7ffU, packet_length);
        };
        // This flushes (if needed) the contents of the inlined data buffer.
        auto const flush_buffer = [&]() {
            if (buffer.empty()) {
                return;
            }
            bits.write(0b111'0000U | ((buffer.size() - 1U) & 0b000'1111U), 7);
            std::ranges::for_each(buffer, write_value);
            buffer.clear();
        };
        // This pushes a value into the buffer, flushing it before if the buffer
        // was full before pushing.
        auto const push_into_buffer = [&](uint16_t const value) {
            if (buffer.size() >= 0xf) {
                flush_buffer();
            }
            buffer.push_back(value);
        };
        constexpr static std::array<uint16_t, 3> const delta_to_code{
                0b110'0000U, 0b100'0000U, 0b101'0000U};
        for (size_t position = 0; position < unpack.size(); position++) {
            uint16_t const value = unpack[position];
            if (value == incrementing_value) {
                flush_buffer();
                size_t const last  = std::min(unpack.size(), position + 15);
                uint16_t     next  = value + 1;
                size_t       count = 0;
                for (size_t i = position + 1; i < last; i++) {
                    if (next != unpack[i]) {
                        break;
                    }
                    next++;
                    count++;
                }
                bits.write(0b00'0000U | (count & 0b00'1111U), 6);
                incrementing_value = next;
                position += count;
                continue;
            }
            if (value == common_value) {
                flush_buffer();
                size_t const   last  = std::min(unpack.size(), position + 15);
                uint16_t const next  = value;
                size_t         count = 0;
                for (size_t i = position + 1; i < last; i++) {
                    if (next != unpack[i]) {
                        break;
                    }
                    count++;
                }
                bits.write(0b01'0000U | (count & 0b00'1111U), 6);
                position += count;
                continue;
            }
            if (position + 1 < unpack.size()) {
                uint16_t       next  = unpack[position + 1];
                uint16_t const delta = next - value;
                uint16_t const index = delta + 1U;
                if (next != incrementing_value && index <= 2U) {
                    flush_buffer();
                    size_t const last  = std::min(unpack.size(), position + 15);
                    size_t       count = 1;
                    next += delta;
                    for (size_t i = position + 2; i < last; i++) {
                        if (next != unpack[i] || next == incrementing_value) {
                            break;
                        }
                        next += delta;
                        count++;
                    }
                    uint16_t const code = delta_to_code[index];
                    bits.write((code | count) & 0b111'1111U, 7);
                    write_value(value);
                    position += count;
                    continue;
                }
            }
            push_into_buffer(value);
        }

        flush_buffer();

        // Terminator.
        bits.write(0b111'1111U, 7);
        bits.flush();
    }
};

bool enigma::decode(std::istream& source, std::ostream& dest) {
    auto const        location = source.tellg();
    std::stringstream input(std::ios::in | std::ios::out | std::ios::binary);
    extract(source, input);

    enigma_internal::decode(input, dest);
    source.seekg(location + input.tellg());
    return true;
}

bool enigma::encode(std::istream& source, std::ostream& dest) {
    enigma_internal::encode(source, dest);
    return true;
}

bool enigma::encode(std::ostream& dest, std::span<uint8_t const> data) {
    std::stringstream source(std::ios::in | std::ios::out | std::ios::binary);
    source.write(reinterpret_cast<char const*>(data.data()), std::ssize(data));
    source.seekg(0);
    return encode(source, dest);
}
