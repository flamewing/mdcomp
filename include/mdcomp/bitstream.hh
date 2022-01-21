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

#include <mdcomp/bigendian_io.hh>
#include <mdcomp/function_ref.hh>

#include <climits>
#include <iosfwd>
#include <limits>

#if defined(_MSC_VER)
#    define INLINE __forceinline
#else
#    define INLINE inline
#endif

namespace detail {
    template <std::unsigned_integral T>
    [[nodiscard, gnu::const, gnu::always_inline]] constexpr INLINE T
            nextMask(T mask, size_t size) noexcept {
        return mask ^ static_cast<T>(mask << size);
    }

    template <std::unsigned_integral T>
    [[nodiscard, gnu::const, gnu::always_inline]] consteval INLINE T getMask() noexcept {
        T mask = std::numeric_limits<T>::max();
        for (size_t size = sizeof(T); size > 1; size >>= 1U) {
            mask = nextMask(mask, size * CHAR_BIT / 2);
        }
        return mask;
    }

    template <size_t size, auto mask, std::unsigned_integral T>
    [[nodiscard, gnu::const, gnu::always_inline]] constexpr INLINE T
            reverseByteBits(T val) noexcept {
        constexpr const size_t new_size = size >> 1U;
        constexpr const auto   factor   = T(1) << new_size;
        constexpr const auto   new_mask = nextMask<T>(mask, new_size);
        if constexpr (size > 1) {
            const T val1    = val & new_mask;
            const T val2    = val ^ val1;
            const T new_val = factor * val1 + val2 / factor;
            return reverseByteBits<new_size, new_mask>(new_val);
        }
        return val;
    }

    template <std::unsigned_integral T>
    [[nodiscard, gnu::const, gnu::always_inline]] constexpr auto reverseBits(
            T val) noexcept {
#ifdef __clang__
        if constexpr (CHAR_BIT == 8) {
            if (!std::is_constant_evaluated()) {
                if constexpr (sizeof(T) == 1) {
                    return val = __builtin_bitreverse8(val);
                }
                if constexpr (sizeof(T) == 2) {
                    return val = __builtin_bitreverse16(val);
                }
                if constexpr (sizeof(T) == 4) {
                    return val = __builtin_bitreverse32(val);
                }
                if constexpr (sizeof(T) == 8) {
                    return val = __builtin_bitreverse64(val);
                }
                if constexpr (sizeof(T) == 16) {
                    if constexpr (__has_builtin(__builtin_bitreverse128)) {
                        return __builtin_bitreverse128(val);
                    }
                    return (__builtin_bitreverse64(val >> 64U)
                            | (static_cast<T>(__builtin_bitreverse64(val)) << 64U));
                }
            }
        }
#endif
        constexpr const T mask = getMask<T>();
        return reverseByteBits<CHAR_BIT, mask>(byteswap(val));
    }

    template <std::signed_integral T>
    [[nodiscard, gnu::const, gnu::always_inline]] constexpr auto reverseBits(
            T val) noexcept {
        return bit_cast<T>(reverseBits(std::make_unsigned_t<T>(val)));
    }

    static_assert(reverseBits<uint8_t>(0x35U) == 0xacU);
    static_assert(reverseBits<uint16_t>(0x1357U) == 0xeac8U);
    static_assert(reverseBits(0x01234567U) == 0xE6A2C480U);
    static_assert(reverseBits(0x0123456789abcdefULL) == 0xf7b3d591E6A2C480ULL);

    template <typename F, typename R, typename... Args>
    concept bit_callback = requires(F mf, Args... as) {
        { mf(as...) } -> std::same_as<R>;
    };
}    // namespace detail

enum class bit_endian
{
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
    size_t readbits;
    uint_t bitbuffer;

    static constexpr const size_t bitcount = sizeof(uint_t) * CHAR_BIT;

    [[nodiscard]] uint_t read_bits() noexcept {
        uint_t bits = reader();
        if constexpr (bit_order == bit_endian::little) {
            return detail::reverseBits(bits);
        } else {
            return bits;
        }
    }
    void check_buffer() noexcept {
        if (readbits != 0U) {
            return;
        }

        bitbuffer = read_bits();
        readbits  = bitcount;
    }

public:
    explicit ibitbuffer(const Reader& r) noexcept
            : reader(r), readbits(bitcount), bitbuffer(read_bits()) {}
    explicit ibitbuffer(Reader&& r) noexcept
            : reader(std::move(r)), readbits(bitcount), bitbuffer(read_bits()) {}
    // Gets a single bit from the buffer. Remembers previously read bits, and
    // gets a new T from the actual buffer once all bits in the current T has
    // been used up.
    [[nodiscard]] uint_t pop() noexcept {
        if constexpr (!EarlyRead) {
            check_buffer();
        }
        --readbits;
        uint_t bit = (bitbuffer >> readbits) & 1U;
        bitbuffer ^= (bit << readbits);
        if constexpr (EarlyRead) {
            check_buffer();
        }
        return bit;
    }
    // Reads up to sizeof(T) * CHAR_BIT bits from the buffer. This remembers
    // previously read bits, and gets another T from the actual buffer once all
    // bits in the current T have been read.
    [[nodiscard]] uint_t read(uint8_t const cnt) noexcept {
        if constexpr (!EarlyRead) {
            check_buffer();
        }
        uint_t bits;
        if (readbits < cnt) {
            size_t delta   = cnt - readbits;
            bits           = bitbuffer << delta;
            bitbuffer      = read_bits();
            readbits       = bitcount - delta;
            uint_t newbits = bitbuffer >> readbits;
            bitbuffer ^= (newbits << readbits);
            bits |= newbits;
        } else {
            readbits -= cnt;
            bits = bitbuffer >> readbits;
            bitbuffer ^= (bits << readbits);
        }
        if constexpr (EarlyRead) {
            check_buffer();
        }
        return bits;
    }
    [[nodiscard]] size_t have_waiting_bits() const noexcept {
        return readbits;
    }
};

// This class allows outputting bits into a buffer.
template <
        std::unsigned_integral uint_t, detail::bit_callback<void, uint_t> Writer,
        bit_endian bit_order>
class obitbuffer {
private:
    Writer writer;
    size_t waitingbits;
    uint_t bitbuffer;

    static constexpr const size_t bitcount = sizeof(uint_t) * CHAR_BIT;
    static constexpr const uint_t all_ones = std::numeric_limits<uint_t>::max();

    void write_bits(uint_t const bits) noexcept {
        if constexpr (bit_order == bit_endian::little) {
            writer(detail::reverseBits(bits));
        } else {
            writer(bits);
        }
    }

public:
    explicit obitbuffer(const Writer& w) noexcept
            : writer(w), waitingbits(0), bitbuffer(0) {}
    explicit obitbuffer(Writer&& w) noexcept
            : writer(std::move(w)), waitingbits(0), bitbuffer(0) {}
    // Puts a single bit into the buffer. Remembers previously written bits, and
    // outputs a T to the actual buffer once there are at least sizeof(T) *
    // CHAR_BIT bits stored in the buffer.
    bool push(uint_t const data) noexcept {
        bitbuffer = (bitbuffer << 1U) | (data & 1U);
        if (++waitingbits >= bitcount) {
            write_bits(bitbuffer);
            waitingbits = 0;
            bitbuffer   = 0;
            return true;
        }
        return false;
    }
    // Writes up to sizeof(T) * CHAR_BIT bits to the buffer. This remembers
    // previously written bits, and outputs a T to the actual buffer once there
    // are at least sizeof(T) * CHAR_BIT bits stored in the buffer.
    bool write(uint_t const data, uint8_t const size) noexcept {
        if (waitingbits + size >= bitcount) {
            size_t delta = bitcount - waitingbits;
            waitingbits  = waitingbits + size % (bitcount);
            uint_t bits  = (bitbuffer << delta) | (data >> waitingbits);
            write_bits(bits);
            bitbuffer = data & (all_ones >> (bitcount - waitingbits));
            return true;
        }
        bitbuffer = (bitbuffer << size) | data;
        waitingbits += size;
        return false;
    }
    // Flushes remaining bits (if any) to the buffer, completing the byte by
    // padding with zeroes.
    bool flush() noexcept {
        if (waitingbits != 0U) {
            bitbuffer <<= ((bitcount)-waitingbits);
            write_bits(bitbuffer);
            waitingbits = 0;
            return true;
        }
        return false;
    }
    [[nodiscard]] size_t have_waiting_bits() const noexcept {
        return waitingbits;
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
    struct BitReader {
        auto operator()() noexcept(noexcept(Endian::template Read<uint_t>(source))) {
            return Endian::template Read<uint_t>(source);
        }
        std::istream& source;
    };

    using Callback  = tl::function_ref<uint_t(void)>;
    using bitbuffer = ibitbuffer<uint_t, Callback, bit_order, EarlyRead>;
    BitReader reader;
    bitbuffer buffer;

public:
    explicit ibitstream(std::istream& source) noexcept : reader(source), buffer(reader) {}
    // Gets a single bit from the stream. Remembers previously read bits, and
    // gets a new T from the actual stream once all bits in the current T has
    // been used up.
    [[nodiscard]] uint_t pop() noexcept {
        return buffer.pop();
    }
    // Reads up to sizeof(T) * CHAR_BIT bits from the stream. This remembers
    // previously read bits, and gets another T from the actual stream once all
    // bits in the current T have been read.
    [[nodiscard]] uint_t read(uint8_t const cnt) noexcept {
        return buffer.read(cnt);
    }
    [[nodiscard]] size_t have_waiting_bits() const noexcept {
        return buffer.have_waiting_bits();
    }
};

// This class allows outputting bits into a stream.
template <std::unsigned_integral uint_t, bit_endian bit_order, typename Endian>
class obitstream {
private:
    struct BitWriter {
        auto operator()(uint_t c) noexcept(noexcept(Endian::Write(dest, c))) {
            return Endian::Write(dest, c);
        }
        std::ostream& dest;
    };

    using Callback  = tl::function_ref<void(uint_t)>;
    using bitbuffer = obitbuffer<uint_t, Callback, bit_order>;
    BitWriter writer;
    bitbuffer buffer;

public:
    explicit obitstream(std::ostream& dest) noexcept : writer(dest), buffer(writer) {}
    // Puts a single bit into the stream. Remembers previously written bits, and
    // outputs a T to the actual stream once there are at least sizeof(T) *
    // CHAR_BIT bits stored in the buffer.
    bool push(uint_t const data) noexcept {
        return buffer.push(data);
    }
    // Writes up to sizeof(T) * CHAR_BIT bits to the stream. This remembers
    // previously written bits, and outputs a T to the actual stream once there
    // are at least sizeof(T) * CHAR_BIT bits stored in the buffer.
    bool write(uint_t const data, uint8_t const size) noexcept {
        return buffer.write(data, size);
    }
    // Flushes remaining bits (if any) to the buffer, completing the byte by
    // padding with zeroes.
    bool flush() noexcept {
        return buffer.flush();
    }
    [[nodiscard]] size_t have_waiting_bits() const noexcept {
        return buffer.have_waiting_bits();
    }
};

#undef INLINE

#endif    // LIB_BITSTREAM_HH
