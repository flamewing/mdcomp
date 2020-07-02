/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
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

#include <climits>
#include <iosfwd>

namespace detail {
#if !defined(__clang__)
#    if defined(_MSC_VER) && _MSC_VER < 1910
// MSVC compiler is not c++14 compliant before 19.10.
#        define CONSTEXPR
#    else
#        define CONSTEXPR constexpr
#    endif
    template <typename T, size_t sz>
    constexpr inline T nextMask(T mask) noexcept {
        return mask ^ (mask << sz);
    }

    template <typename T, size_t sz>
    struct reverseByteBits {
        CONSTEXPR inline T operator()(T val, T mask) const noexcept {
            constexpr const size_t nsz = sz >> 1;
            mask                       = nextMask<T, nsz>(mask);
            T                 val1     = (val & mask);
            T                 val2     = val ^ val1;
            constexpr const T factor   = T(1) << nsz;
            val                        = factor * val1 + val2 / factor;
            return reverseByteBits<T, nsz>{}(val, mask);
        }
    };

    template <typename T>
    struct reverseByteBits<T, 1> {
        constexpr inline T operator()(T val, T /*mask*/) const noexcept {
            return val;
        }
    };

    template <typename T, size_t sz>
    struct getMask {
        constexpr inline T operator()(T mask) const noexcept {
            return getMask<T, (sz >> 1)>{}(nextMask<T, (sz >> 1)>(mask));
        }
    };

    template <typename T>
    struct getMask<T, CHAR_BIT> {
        constexpr inline T operator()(T mask) const noexcept {
            return mask;
        }
    };
#endif

    template <typename T>
    auto reverseBits(T val) noexcept
            -> std::enable_if_t<std::is_unsigned<T>::value, T> {
#ifdef __clang__
        if (sizeof(T) == 1) {
            val = __builtin_bitreverse8(val);
        } else if (sizeof(T) == 2) {
            val = __builtin_bitreverse16(val);
        } else if (sizeof(T) == 4) {
            val = __builtin_bitreverse32(val);
        } else if (sizeof(T) == 8) {
            val = __builtin_bitreverse64(val);
        }
        return val;
#else
        constexpr size_t sz   = CHAR_BIT;    // bit size; must be power of 2
        constexpr T      mask = getMask<T, sizeof(T) * CHAR_BIT>{}(~T(0));
        val                   = bswap(val);
        return reverseByteBits<T, sz>{}(val, mask);
#endif
    }

    template <typename T>
    auto reverseBits(T val) noexcept
            -> std::enable_if_t<std::is_signed<T>::value, T> {
        return reverseBits(std::make_unsigned_t<T>(val));
    }
}    // namespace detail

// This class allows reading bits from a stream.
// "EarlyRead" means, in this context, to read a new T as soon as the old one
// runs out of bits; the alternative is to read when a new bit is needed.
template <
        typename T, bool EarlyRead, bool LittleEndianBits = false,
        typename Endian = BigEndian>
class ibitstream {
private:
    std::istream& src;
    size_t        readbits;
    T             bitbuffer;
    size_t        read() noexcept {
        return Endian::template ReadN<std::istream&, sizeof(T)>(src);
    }
    T read_bits() noexcept {
        T bits = read();
        return LittleEndianBits ? detail::reverseBits(bits) : bits;
    }
    void check_buffer() noexcept {
        if (readbits != 0U) {
            return;
        }

        bitbuffer = read_bits();
        readbits  = sizeof(T) * CHAR_BIT;
    }

public:
    explicit ibitstream(std::istream& s) noexcept
            : src(s), readbits(sizeof(T) * CHAR_BIT) {
        bitbuffer = read_bits();
    }
    ibitstream(ibitstream const&)     = delete;
    ibitstream(ibitstream&&) noexcept = delete;
    ibitstream& operator=(ibitstream const&) = delete;
    ibitstream& operator=(ibitstream&&) noexcept = delete;
    // Destructor.
    ~ibitstream() noexcept = default;
    // Gets a single bit from the stream. Remembers previously read bits, and
    // gets a new T from the actual stream once all bits in the current T has
    // been used up.
    T pop() noexcept {
        if (!EarlyRead) {
            check_buffer();
        }
        --readbits;
        T bit = (bitbuffer >> readbits) & 1;
        bitbuffer ^= (bit << readbits);
        if (EarlyRead) {
            check_buffer();
        }
        return bit;
    }
    // Reads up to sizeof(T) * CHAR_BIT bits from the stream. This remembers
    // previously read bits, and gets another T from the actual stream once all
    // bits in the current T have been read.
    T read(uint8_t const cnt) noexcept {
        if (!EarlyRead) {
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
        if (EarlyRead) {
            check_buffer();
        }
        return bits;
    }
    size_t have_waiting_bits() const noexcept {
        return readbits;
    }
};

// This class allows outputting bits into a stream.
template <
        typename T, bool LittleEndianBits = false, typename Endian = BigEndian>
class obitstream {
private:
    std::ostream& dst;
    size_t        waitingbits;
    T             bitbuffer;

    void write(T const c) noexcept {
        Endian::template WriteN<std::ostream&, sizeof(T)>(dst, c);
    }
    void write_bits(T const bits) noexcept {
        write(LittleEndianBits ? detail::reverseBits(bits) : bits);
    }

public:
    explicit obitstream(std::ostream& d) noexcept
            : dst(d), waitingbits(0), bitbuffer(0) {}
    obitstream(obitstream const&)     = delete;
    obitstream(obitstream&&) noexcept = delete;
    obitstream& operator=(obitstream const&) = delete;
    obitstream& operator=(obitstream&&) noexcept = delete;
    // Destructor.
    ~obitstream() noexcept = default;
    // Puts a single bit into the stream. Remembers previously written bits, and
    // outputs a T to the actual stream once there are at least sizeof(T) *
    // CHAR_BIT bits stored in the buffer.
    bool push(T const data) noexcept {
        bitbuffer = (bitbuffer << 1) | (data & 1);
        if (++waitingbits >= sizeof(T) * CHAR_BIT) {
            write_bits(bitbuffer);
            waitingbits = 0;
            bitbuffer   = 0;
            return true;
        }
        return false;
    }
    // Writes up to sizeof(T) * CHAR_BIT bits to the stream. This remembers
    // previously written bits, and outputs a T to the actual stream once there
    // are at least sizeof(T) * CHAR_BIT bits stored in the buffer.
    bool write(T const data, uint8_t const size) noexcept {
        if (waitingbits + size >= sizeof(T) * CHAR_BIT) {
            size_t delta = (sizeof(T) * CHAR_BIT - waitingbits);
            waitingbits  = (waitingbits + size) % (sizeof(T) * CHAR_BIT);
            T bits       = (bitbuffer << delta) | (data >> waitingbits);
            write_bits(bits);
            bitbuffer
                    = (data & (T(~0) >> (sizeof(T) * CHAR_BIT - waitingbits)));
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
    size_t have_waiting_bits() const noexcept {
        return waitingbits;
    }
};

#endif    // LIB_BITSTREAM_HH
