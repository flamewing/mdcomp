/*
 * Copyright (C) Flamewing 2011-2016 <flamewing.sonic@gmail.com>
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

#ifndef LIB_BITSTREAM_HH
#define LIB_BITSTREAM_HH

#include "mdcomp/bigendian_io.hh"
#include "mdcomp/function_ref.hh"

#include <climits>
#include <iosfwd>
#include <limits>

#if defined(_MSC_VER)
#    define INLINE       __forceinline
#    define CONST_INLINE __forceinline
// #    define PURE_INLINE  __forceinline
#elif defined(__GNUG__)
#    define INLINE       [[gnu::always_inline]] inline
#    define CONST_INLINE [[gnu::const, gnu::always_inline]] inline
// #    define PURE_INLINE  [[gnu::pure, gnu::always_inline]] inline
#else
#    define INLINE       inline
#    define CONST_INLINE inline
// #    define PURE_INLINE  inline
#endif

namespace detail {
    template <std::unsigned_integral uint_t>
    [[nodiscard]] CONST_INLINE constexpr uint_t nextMask(
            uint_t mask, size_t size) noexcept {
        return mask ^ static_cast<uint_t>(mask << size);
    }

    template <std::unsigned_integral uint_t>
    [[nodiscard]] CONST_INLINE consteval uint_t getMask() noexcept {
        uint_t mask = std::numeric_limits<uint_t>::max();
        for (size_t size = sizeof(uint_t); size > 1; size >>= 1U) {
            mask = nextMask(mask, size * CHAR_BIT / 2);
        }
        return mask;
    }

    template <size_t size, auto mask, std::unsigned_integral uint_t>
    [[nodiscard]] CONST_INLINE constexpr uint_t reverseByteBits(uint_t value) noexcept {
        constexpr size_t const new_size = size >> 1U;
        constexpr auto const   factor   = uint_t{1} << new_size;
        constexpr auto const   new_mask = nextMask<uint_t>(mask, new_size);
        if constexpr (size > 1) {
            uint_t const val1    = value & new_mask;
            uint_t const val2    = value ^ val1;
            uint_t const new_val = factor * val1 + val2 / factor;
            return reverseByteBits<new_size, new_mask>(new_val);
        }
        return value;
    }

    template <std::unsigned_integral uint_t>
    [[nodiscard]] CONST_INLINE constexpr auto reverseBits(uint_t value) noexcept {
#ifdef __clang__
        if constexpr (CHAR_BIT == 8) {
            if (!std::is_constant_evaluated()) {
                if constexpr (sizeof(uint_t) == 1) {
                    return value = __builtin_bitreverse8(value);
                }
                if constexpr (sizeof(uint_t) == 2) {
                    return value = __builtin_bitreverse16(value);
                }
                if constexpr (sizeof(uint_t) == 4) {
                    return value = __builtin_bitreverse32(value);
                }
                if constexpr (sizeof(uint_t) == 8) {
                    return value = __builtin_bitreverse64(value);
                }
                if constexpr (sizeof(uint_t) == 16) {
                    if constexpr (__has_builtin(__builtin_bitreverse128)) {
                        return __builtin_bitreverse128(value);
                    }
                    return (__builtin_bitreverse64(value >> 64U)
                            | (static_cast<uint_t>(__builtin_bitreverse64(value))
                               << 64U));
                }
            }
        }
#endif
        constexpr auto const mask = getMask<uint_t>();
        return reverseByteBits<CHAR_BIT, mask>(byteswap(value));
    }

    template <std::signed_integral int_t>
    [[nodiscard]] CONST_INLINE constexpr auto reverseBits(int_t value) noexcept {
        return detail::bit_cast<int_t>(reverseBits(std::make_unsigned_t<int_t>(value)));
    }

    static_assert(reverseBits<uint8_t>(0x35U) == 0xacU);
    static_assert(reverseBits<uint16_t>(0x1357U) == 0xeac8U);
    static_assert(reverseBits(0x01234567U) == 0xE6A2C480U);
    static_assert(reverseBits(0x0123456789abcdefULL) == 0xf7b3d591E6A2C480ULL);

    template <typename F, typename R, typename... Args>
    concept bit_callback = requires(F callable, Args... arguments) {
        { callable(arguments...) } -> std::same_as<R>;
    };
}    // namespace detail

enum class bit_endian {
    little,
    big
};

// This class allows reading bits from a buffer.
// "EarlyRead" means, in this context, to read a new T as soon as the old one
// runs out of bits; the alternative is to read when a new bit is needed.
template <
        std::unsigned_integral uint_t, detail::bit_callback<uint_t> Reader,
        bit_endian bit_order, bool EarlyRead>
class ibitbuffer {
private:
    Reader reader;
    size_t num_read_bits;
    uint_t bit_buffer;

    constexpr static inline size_t const bit_count = sizeof(uint_t) * CHAR_BIT;

    [[nodiscard]] INLINE uint_t read_bits() noexcept(noexcept(reader())) {
        uint_t bits = reader();
        if constexpr (bit_order == bit_endian::little) {
            return detail::reverseBits(bits);
        } else {
            return bits;
        }
    }

    INLINE void check_buffer() noexcept(noexcept(read_bits())) {
        if (num_read_bits != 0U) {
            return;
        }

        bit_buffer    = read_bits();
        num_read_bits = bit_count;
    }

public:
    explicit ibitbuffer(Reader const& reader_) noexcept(noexcept(read_bits()))
            : reader(reader_), num_read_bits(bit_count), bit_buffer(read_bits()) {}

    explicit ibitbuffer(Reader&& reader_) noexcept(noexcept(read_bits()))
            : reader(std::move(reader_)), num_read_bits(bit_count),
              bit_buffer(read_bits()) {}

    // Gets a single bit from the buffer. Remembers previously read bits, and
    // gets a new T from the actual buffer once all bits in the current T has
    // been used up.
    [[nodiscard]] INLINE uint_t pop() noexcept(noexcept(read_bits())) {
        if constexpr (!EarlyRead) {
            check_buffer();
        }
        --num_read_bits;
        uint_t bit = (bit_buffer >> num_read_bits) & 1U;
        bit_buffer ^= (bit << num_read_bits);
        if constexpr (EarlyRead) {
            check_buffer();
        }
        return bit;
    }

    // Reads up to sizeof(T) * CHAR_BIT bits from the buffer. This remembers
    // previously read bits, and gets another T from the actual buffer once all
    // bits in the current T have been read.
    [[nodiscard]] INLINE uint_t read(size_t const count) noexcept(noexcept(read_bits())) {
        if constexpr (!EarlyRead) {
            check_buffer();
        }
        uint_t bits;
        if (num_read_bits < count) {
            size_t delta    = count - num_read_bits;
            bits            = static_cast<uint_t>(bit_buffer << delta);
            bit_buffer      = read_bits();
            num_read_bits   = bit_count - delta;
            uint_t new_bits = bit_buffer >> num_read_bits;
            bit_buffer ^= (new_bits << num_read_bits);
            bits |= new_bits;
        } else {
            num_read_bits -= count;
            bits = bit_buffer >> num_read_bits;
            bit_buffer ^= (bits << num_read_bits);
        }
        if constexpr (EarlyRead) {
            check_buffer();
        }
        return bits;
    }

    [[nodiscard]] INLINE size_t have_waiting_bits() const noexcept {
        return num_read_bits;
    }
};

// This class allows outputting bits into a buffer.
template <
        std::unsigned_integral uint_t, detail::bit_callback<void, uint_t> Writer,
        bit_endian bit_order>
class obitbuffer {
private:
    Writer writer;
    size_t waiting_bits;
    uint_t bit_buffer;

    constexpr static inline size_t const bit_count = sizeof(uint_t) * CHAR_BIT;
    constexpr static inline uint_t const all_ones  = std::numeric_limits<uint_t>::max();

    INLINE void write_bits(uint_t const bits) noexcept(noexcept(writer(bits))) {
        if constexpr (bit_order == bit_endian::little) {
            writer(detail::reverseBits(bits));
        } else {
            writer(bits);
        }
    }

public:
    explicit obitbuffer(Writer const& writer_) noexcept
            : writer(writer_), waiting_bits(0), bit_buffer(0) {}

    explicit obitbuffer(Writer&& writer_) noexcept
            : writer(std::move(writer_)), waiting_bits(0), bit_buffer(0) {}

    // Puts a single bit into the buffer. Remembers previously written bits, and
    // outputs a T to the actual buffer once there are at least sizeof(T) *
    // CHAR_BIT bits stored in the buffer.
    INLINE bool push(uint_t const data) noexcept(noexcept(write_bits(bit_buffer))) {
        bit_buffer = static_cast<uint_t>(bit_buffer << 1U) | (data & 1U);
        if (++waiting_bits >= bit_count) {
            write_bits(bit_buffer);
            waiting_bits = 0;
            bit_buffer   = 0;
            return true;
        }
        return false;
    }

    // Writes up to sizeof(T) * CHAR_BIT bits to the buffer. This remembers
    // previously written bits, and outputs a T to the actual buffer once there
    // are at least sizeof(T) * CHAR_BIT bits stored in the buffer.
    INLINE bool write(uint_t const data, size_t const size) noexcept(
            noexcept(write_bits(bit_buffer))) {
        if (waiting_bits + size >= bit_count) {
            size_t delta = bit_count - waiting_bits;
            waiting_bits = (waiting_bits + size) % bit_count;
            uint_t bits
                    = static_cast<uint_t>(bit_buffer << delta) | (data >> waiting_bits);
            write_bits(bits);
            bit_buffer = data & (all_ones >> (bit_count - waiting_bits));
            return true;
        }
        bit_buffer = static_cast<uint_t>(bit_buffer << size) | data;
        waiting_bits += size;
        return false;
    }

    // Flushes remaining bits (if any) to the buffer, completing the byte by
    // padding with zeroes.
    INLINE bool flush() noexcept(noexcept(write_bits(bit_buffer))) {
        if (waiting_bits != 0U) {
            bit_buffer <<= ((bit_count)-waiting_bits);
            write_bits(bit_buffer);
            waiting_bits = 0;
            return true;
        }
        return false;
    }

    [[nodiscard]] INLINE size_t have_waiting_bits() const noexcept {
        return waiting_bits;
    }
};

// This class allows reading bits from a stream.
// "EarlyRead" means, in this context, to read a new T as soon as the old one
// runs out of bits; the alternative is to read when a new bit is needed.
template <
        std::unsigned_integral uint_t, bit_endian bit_order, typename Endian,
        bool EarlyRead>
class ibitstream {
private:
    constexpr static inline bool const is_noexcept
            = noexcept(Endian::template Read<uint_t>(std::declval<std::istream&>()));

    struct BitReader {
        auto operator()() noexcept(is_noexcept) {
            return Endian::template Read<uint_t>(source);
        }

        std::istream& source;
    };

    using Callback   = std23::function_ref<uint_t(void) noexcept(is_noexcept)>;
    using bit_buffer = ibitbuffer<uint_t, Callback, bit_order, EarlyRead>;
    BitReader  reader;
    bit_buffer buffer;

public:
    explicit ibitstream(std::istream& source) noexcept(noexcept(bit_buffer(reader)))
            : reader{source}, buffer(reader) {}

    // Gets a single bit from the stream. Remembers previously read bits, and
    // gets a new T from the actual stream once all bits in the current T has
    // been used up.
    [[nodiscard]] INLINE uint_t pop() noexcept(noexcept(buffer.pop())) {
        return buffer.pop();
    }

    // Reads up to sizeof(T) * CHAR_BIT bits from the stream. This remembers
    // previously read bits, and gets another T from the actual stream once all
    // bits in the current T have been read.
    [[nodiscard]] INLINE uint_t
            read(size_t const count) noexcept(noexcept(buffer.read(count))) {
        return buffer.read(count);
    }

    [[nodiscard]] INLINE size_t have_waiting_bits() const
            noexcept(noexcept(buffer.have_waiting_bits())) {
        return buffer.have_waiting_bits();
    }
};

// This class allows outputting bits into a stream.
template <std::unsigned_integral uint_t, bit_endian bit_order, typename Endian>
class obitstream {
private:
    constexpr static inline bool const is_noexcept = noexcept(
            Endian::Write(std::declval<std::ostream&>(), std::declval<uint_t>()));

    struct BitWriter {
        auto operator()(uint_t count) noexcept(is_noexcept) {
            return Endian::Write(dest, count);
        }

        std::ostream& dest;
    };

    using Callback   = std23::function_ref<void(uint_t) noexcept(is_noexcept)>;
    using bit_buffer = obitbuffer<uint_t, Callback, bit_order>;
    BitWriter  writer;
    bit_buffer buffer;

public:
    explicit obitstream(std::ostream& dest) noexcept(noexcept(bit_buffer(writer)))
            : writer{dest}, buffer(writer) {}

    // Puts a single bit into the stream. Remembers previously written bits, and
    // outputs a T to the actual stream once there are at least sizeof(T) *
    // CHAR_BIT bits stored in the buffer.
    INLINE bool push(uint_t const data) noexcept(noexcept(buffer.push(data))) {
        return buffer.push(data);
    }

    // Writes up to sizeof(T) * CHAR_BIT bits to the stream. This remembers
    // previously written bits, and outputs a T to the actual stream once there
    // are at least sizeof(T) * CHAR_BIT bits stored in the buffer.
    INLINE bool write(uint_t const data, size_t const size) noexcept(
            noexcept(buffer.write(data, size))) {
        return buffer.write(data, size);
    }

    // Flushes remaining bits (if any) to the buffer, completing the byte by
    // padding with zeroes.
    INLINE bool flush() noexcept {
        return buffer.flush();
    }

    [[nodiscard]] INLINE size_t have_waiting_bits() const
            noexcept(noexcept(buffer.have_waiting_bits())) {
        return buffer.have_waiting_bits();
    }
};

#undef INLINE
#undef CONST_INLINE
// #undef PURE_INLINE

#endif    // LIB_BITSTREAM_HH
