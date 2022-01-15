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
    [[nodiscard, gnu::const, gnu::always_inline]] consteval INLINE T
            getMask() noexcept {
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
                            | (static_cast<T>(__builtin_bitreverse64(val))
                               << 64U));
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

// This class allows reading bits from a buffer.
// "EarlyRead" means, in this context, to read a new T as soon as the old one
// runs out of bits; the alternative is to read when a new bit is needed.
template <
        std::unsigned_integral T, detail::bit_callback<T> Reader, bool EarlyRead,
        bool LittleEndianBits>
class ibitbuffer {
private:
    Reader reader;
    size_t readbits;
    T      bitbuffer;

    [[nodiscard]] T read_bits() noexcept {
        T bits = reader();
        if constexpr (LittleEndianBits) {
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
        readbits  = sizeof(T) * CHAR_BIT;
    }

public:
    explicit ibitbuffer(const Reader& r) noexcept
            : reader(r), readbits(sizeof(T) * CHAR_BIT),
              bitbuffer(read_bits()) {}
    explicit ibitbuffer(Reader&& r) noexcept
            : reader(std::move(r)), readbits(sizeof(T) * CHAR_BIT),
              bitbuffer(read_bits()) {}
    // Gets a single bit from the buffer. Remembers previously read bits, and
    // gets a new T from the actual buffer once all bits in the current T has
    // been used up.
    [[nodiscard]] T pop() noexcept {
        if constexpr (!EarlyRead) {
            check_buffer();
        }
        --readbits;
        T bit = (bitbuffer >> readbits) & 1U;
        bitbuffer ^= (bit << readbits);
        if constexpr (EarlyRead) {
            check_buffer();
        }
        return bit;
    }
    // Reads up to sizeof(T) * CHAR_BIT bits from the buffer. This remembers
    // previously read bits, and gets another T from the actual buffer once all
    // bits in the current T have been read.
    [[nodiscard]] T read(uint8_t const cnt) noexcept {
        if constexpr (!EarlyRead) {
            check_buffer();
        }
        T bits;
        if (readbits < cnt) {
            size_t delta = (cnt - readbits);
            bits         = bitbuffer << delta;
            bitbuffer    = read_bits();
            readbits     = (sizeof(T) * CHAR_BIT) - delta;
            T newbits    = (bitbuffer >> readbits);
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
template <std::unsigned_integral T, detail::bit_callback<void, T> Writer, bool LittleEndianBits>
class obitbuffer {
private:
    Writer writer;
    size_t waitingbits;
    T      bitbuffer;

    void write_bits(T const bits) noexcept {
        if constexpr (LittleEndianBits) {
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
    bool push(T const data) noexcept {
        bitbuffer = (bitbuffer << 1U) | (data & 1U);
        if (++waitingbits >= sizeof(T) * CHAR_BIT) {
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
    bool write(T const data, uint8_t const size) noexcept {
        if (waitingbits + size >= sizeof(T) * CHAR_BIT) {
            size_t delta = (sizeof(T) * CHAR_BIT - waitingbits);
            waitingbits  = (waitingbits + size) % (sizeof(T) * CHAR_BIT);
            T bits       = (bitbuffer << delta) | (data >> waitingbits);
            write_bits(bits);
            constexpr const T ones = std::numeric_limits<T>::max();
            bitbuffer = (data & (ones >> (sizeof(T) * CHAR_BIT - waitingbits)));
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
            bitbuffer <<= ((sizeof(T) * CHAR_BIT) - waitingbits);
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
        std::unsigned_integral T, bool EarlyRead, bool LittleEndianBits = false,
        typename Endian = BigEndian>
class ibitstream {
private:
    struct BitReader {
        std::istream& src;
        T operator()() noexcept(noexcept(Endian::template Read<T>(src))) {
            return Endian::template Read<T>(src);
        }
    };

    using Callback    = tl::function_ref<T(void)>;
    using bitbuffer_t = ibitbuffer<T, Callback, EarlyRead, LittleEndianBits>;
    std::istream& src;
    BitReader     reader;
    bitbuffer_t   buffer;

public:
    explicit ibitstream(std::istream& s) noexcept
            : src(s), reader(s), buffer(reader) {}
    // Gets a single bit from the stream. Remembers previously read bits, and
    // gets a new T from the actual stream once all bits in the current T has
    // been used up.
    [[nodiscard]] T pop() noexcept {
        return buffer.pop();
    }
    // Reads up to sizeof(T) * CHAR_BIT bits from the stream. This remembers
    // previously read bits, and gets another T from the actual stream once all
    // bits in the current T have been read.
    [[nodiscard]] T read(uint8_t const cnt) noexcept {
        return buffer.read(cnt);
    }
    [[nodiscard]] size_t have_waiting_bits() const noexcept {
        return buffer.have_waiting_bits();
    }
};

// This class allows outputting bits into a stream.
template <
        std::unsigned_integral T, bool LittleEndianBits = false,
        typename Endian = BigEndian>
class obitstream {
private:
    struct BitWriter {
        std::ostream& dst;
        auto operator()(T c) noexcept(noexcept(Endian::Write(dst, c))) {
            return Endian::Write(dst, c);
        }
    };

    using Callback    = tl::function_ref<void(T)>;
    using bitbuffer_t = obitbuffer<T, Callback, LittleEndianBits>;
    std::ostream& dst;
    BitWriter     writer;
    bitbuffer_t   buffer;

public:
    explicit obitstream(std::ostream& d) noexcept
            : dst(d), writer(d), buffer(writer) {}
    // Puts a single bit into the stream. Remembers previously written bits, and
    // outputs a T to the actual stream once there are at least sizeof(T) *
    // CHAR_BIT bits stored in the buffer.
    bool push(T const data) noexcept {
        return buffer.push(data);
    }
    // Writes up to sizeof(T) * CHAR_BIT bits to the stream. This remembers
    // previously written bits, and outputs a T to the actual stream once there
    // are at least sizeof(T) * CHAR_BIT bits stored in the buffer.
    bool write(T const data, uint8_t const size) noexcept {
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
