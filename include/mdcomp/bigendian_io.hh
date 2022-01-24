/*
 * Copyright (C) Flamewing 2011-2022 <flamewing.sonic@gmail.com>
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIB_BIGENDIAN_IO_HH
#define LIB_BIGENDIAN_IO_HH

#include <boost/mp11.hpp>
#include <boost/mpl/has_xxx.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <concepts>
#include <cstring>
#include <iterator>
#include <limits>
#include <type_traits>

#ifdef _LIBCPP_VERSION
namespace std {
    using streamsize = ptrdiff_t;
}
#elif defined(_MSC_VER)
#    include <stdlib.h>
#    pragma intrinsic(_byteswap_ushort)
#    pragma intrinsic(_byteswap_ulong)
#    pragma intrinsic(_byteswap_uint64)
#elif defined(__INTEL_COMPILER)
#    include <byteswap.h>
#endif

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
    // Meta-programming stuff.

    template <typename Iter>
    concept byte_input_iterator = requires() {
        requires std::input_iterator<Iter>;
        requires(std::is_same_v<std::iter_value_t<Iter>, char>)
                || (std::is_same_v<std::iter_value_t<Iter>, uint8_t>);
    };

    template <typename Iter>
    concept byte_output_iterator
            = (std::output_iterator<Iter, uint8_t>) || (std::output_iterator<Iter, char>);

    template <typename T>
    concept container = requires(T a, const T b) {
        requires std::regular<T>;
        requires std::swappable<T>;
        requires std::destructible<typename T::value_type>;
        requires std::same_as<typename T::reference, typename T::value_type&>;
        requires std::same_as<typename T::const_reference, const typename T::value_type&>;
        requires std::forward_iterator<typename T::iterator>;
        requires std::forward_iterator<typename T::const_iterator>;
        requires std::signed_integral<typename T::difference_type>;
        requires std::same_as<
                typename T::difference_type,
                typename std::iterator_traits<typename T::iterator>::difference_type>;
        requires std::same_as<
                typename T::difference_type,
                typename std::iterator_traits<
                        typename T::const_iterator>::difference_type>;
        { a.begin() } -> std::same_as<typename T::iterator>;
        { a.end() } -> std::same_as<typename T::iterator>;
        { b.begin() } -> std::same_as<typename T::const_iterator>;
        { b.end() } -> std::same_as<typename T::const_iterator>;
        { a.cbegin() } -> std::same_as<typename T::const_iterator>;
        { a.cend() } -> std::same_as<typename T::const_iterator>;
        { a.size() } -> std::same_as<typename T::size_type>;
        { a.max_size() } -> std::same_as<typename T::size_type>;
        { a.empty() } -> std::same_as<bool>;
    };

    template <typename T>
    concept contiguous_container = container<T> && requires(
            T a, typename T::size_type n, typename T::value_type v) {
        requires std::contiguous_iterator<typename T::iterator>;
        requires std::contiguous_iterator<typename T::const_iterator>;
        { a.resize(n) } -> std::same_as<void>;
        {*a.data() = v};
    };

    template <size_t Size>
    constexpr inline auto select_unsigned() noexcept {
        static_assert(std::has_single_bit(Size), "Size must be a power of 2");
        static_assert(
                Size > 0 && Size <= sizeof(uint64_t),
                "Size must be between 1 and sizeof(uint64_t)");
        if constexpr (Size == sizeof(uint8_t)) {
            return uint8_t{};
        } else if constexpr (Size == sizeof(uint16_t)) {
            return uint16_t{};
        } else if constexpr (Size == sizeof(uint32_t)) {
            return uint32_t{};
        } else {
            return uint64_t{};
        }
    }

    template <size_t Size>
    using select_unsigned_t = decltype(select_unsigned<Size>());

    // Note: fails for stupid stuff like
    // std::reverse_iterator<std::reverse_iterator<T>>. This failure is
    // intentional.
    template <typename T>
    struct is_reverse_iterator : std::false_type {};

    template <typename T>
    struct is_reverse_iterator<std::reverse_iterator<T>> : std::true_type {};

    template <typename T>
    struct is_reverse_iterator<const T> : is_reverse_iterator<T> {};

    template <typename T>
    struct is_reverse_iterator<T&> : is_reverse_iterator<T> {};

    template <typename T>
    constexpr const inline bool is_reverse_iterator_v = is_reverse_iterator<T>::value;

    template <typename T>
    concept contiguous_reverse_iterator = (is_reverse_iterator_v<T>)&&(
            std::contiguous_iterator<typename T::iterator_type>);

    // Mashed together implementation based on libstdc++/libc++/MS STL.
    // GCC/clang both have a 128-bit integer type, which this implementation
    // supports; but MSVC compiler does not support a 128-bit integer, so this
    // is not portable.
    template <std::integral T>
    [[nodiscard]] CONST_INLINE constexpr T byteswap(T val) noexcept {
        if constexpr (CHAR_BIT == 8) {
            if constexpr (sizeof(T) == 1) {
                return val;
            }
            if (!std::is_constant_evaluated()) {
                if constexpr (sizeof(T) == 2) {
#ifdef __GNUG__
                    return __builtin_bswap16(val);
#elif defined(_MSC_VER)
                    return _byteswap_ushort(val);
#endif
                }
                if constexpr (sizeof(T) == 4) {
#ifdef __GNUG__
                    return __builtin_bswap32(val);
#elif defined(_MSC_VER)
                    return _byteswap_ulong(val);
#endif
                }
                if constexpr (sizeof(T) == 8) {
#ifdef __GNUG__
                    return __builtin_bswap64(val);
#elif defined(_MSC_VER)
                    return _byteswap_uint64(val);
#endif
                }
#ifdef __GNUG__
                if constexpr (sizeof(T) == 16) {
                    if constexpr (__has_builtin(__builtin_bswap128)) {
                        return __builtin_bswap128(val);
                    }
                    return (__builtin_bswap64(val >> 64U)
                            | (static_cast<T>(__builtin_bswap64(val)) << 64U));
                }
#endif
            }
        }

        using uint_t = std::make_unsigned_t<std::remove_cv_t<T>>;
        // Fallback implementation that handles even __int24 etc.
        size_t nbits = CHAR_BIT;
        size_t diff  = nbits * (sizeof(T) - 1);
        size_t mask1 = static_cast<unsigned char>(~0U);
        size_t mask2 = mask1 << diff;
        size_t value = val;
        for (size_t ii = 0; ii < sizeof(T) / 2; ++ii) {
            size_t byte1 = value & mask1;
            size_t byte2 = value & mask2;
            value        = static_cast<uint_t>(
                    value ^ byte1 ^ byte2 ^ (byte1 << diff) ^ (byte2 >> diff));
            mask1 = static_cast<uint_t>(mask1 << nbits);
            mask2 = static_cast<uint_t>(mask2 >> nbits);
            diff -= 2ULL * nbits;
        }
        return uint_t(value & std::numeric_limits<uint_t>::max());
    }

    template <typename To, typename From>
        requires requires() {
            sizeof(To) == sizeof(From);
            std::is_trivially_copyable_v<To>;
            std::is_trivially_copyable_v<From>;
        }
    [[nodiscard]] CONST_INLINE constexpr To bit_cast(const From& from) noexcept {
#if defined(__cpp_lib_bit_cast) && __cpp_lib_bit_cast >= 201806L
        return std::bit_cast<To>(from);
#elif __has_builtin(__builtin_bit_cast)
        return __builtin_bit_cast(To, from);
#else
#    error "Compiler supports neither std::bit_cast nor __builtin_bit_cast"
#endif
    }

    template <std::endian endian>
    struct EndianBase {
    private:
        template <std::unsigned_integral To, typename Stream>
            requires requires(Stream s, char* p, std::streamsize c) {
                { s.read(p, c) } -> std::common_reference_with<Stream>;
            }
        [[nodiscard]] INLINE constexpr static To ReadImpl(Stream&& in) noexcept(
                noexcept(in.read(std::declval<char*>(), sizeof(To)))) {
            alignas(alignof(To)) std::array<char, sizeof(To)> buffer;
            in.read(buffer.data(), sizeof(To));
            if constexpr (endian != std::endian::native) {
                return detail::byteswap(bit_cast<To>(buffer));
            } else {
                return bit_cast<To>(buffer);
            }
        }

        template <std::unsigned_integral To, typename Stream>
            requires requires(Stream s, char* p, std::streamsize c) {
                { s.sgetn(p, c) } -> std::same_as<std::streamsize>;
            }
        [[nodiscard]] INLINE constexpr static To ReadImpl(Stream&& in) noexcept(
                noexcept(in.sgetn(std::declval<char*>(), sizeof(To)))) {
            alignas(alignof(To)) std::array<char, sizeof(To)> buffer;
            in.sgetn(buffer.data(), sizeof(To));
            if constexpr (endian != std::endian::native) {
                return detail::byteswap(bit_cast<To>(buffer));
            } else {
                return bit_cast<To>(buffer);
            }
        }

        template <std::unsigned_integral To, typename IterRef>
            requires(byte_input_iterator<std::remove_cvref_t<IterRef>>)
        [[nodiscard]] INLINE constexpr static To ReadImpl(IterRef&& in) noexcept {
            using Iter = std::remove_cvref_t<IterRef>;
            if constexpr (contiguous_reverse_iterator<Iter>) {
                std::advance(in, sizeof(To));
            }
            alignas(alignof(To)) std::array<char, sizeof(To)> buffer{};
            // Both of these versions generate optimal code in GCC and
            // clang. I am splitting these cases because MSVC compiler does
            // cannot see through the iterator abstraction.
            if constexpr (
                    (std::contiguous_iterator<Iter>)
                    || (contiguous_reverse_iterator<Iter>)) {
                std::copy_n(std::to_address(in), sizeof(To), buffer.data());
            } else {
                std::copy_n(in, sizeof(To), std::begin(buffer));
            }
            const To val = [&]() {
                if constexpr (contiguous_reverse_iterator<Iter>) {
                    if constexpr (endian == std::endian::native) {
                        return detail::byteswap(bit_cast<To>(buffer));
                    } else {
                        return bit_cast<To>(buffer);
                    }
                } else {
                    if constexpr (endian != std::endian::native) {
                        return detail::byteswap(bit_cast<To>(buffer));
                    } else {
                        return bit_cast<To>(buffer);
                    }
                }
            }();
            if constexpr ((std::forward_iterator<Iter>)&&(
                                  !contiguous_reverse_iterator<Iter>)) {
                std::advance(in, sizeof(To));
            }
            return val;
        }

        template <std::unsigned_integral From, typename Stream>
            requires requires(Stream s, const char* p, std::streamsize c) {
                { s.write(p, c) } -> std::common_reference_with<Stream>;
            }
        INLINE constexpr static void WriteImpl(Stream&& out, From val) noexcept(
                noexcept(out.write(std::declval<const char*>(), sizeof(From)))) {
            if constexpr (endian != std::endian::native) {
                val = detail::byteswap(val);
            }
            alignas(alignof(From)) std::array<char, sizeof(From)> buffer
                    = detail::bit_cast<decltype(buffer)>(val);
            out.write(buffer.data(), sizeof(From));
        }

        template <std::unsigned_integral From, typename Stream>
            requires requires(Stream s, const char* p, std::streamsize c) {
                { s.sputn(p, c) } -> std::same_as<std::streamsize>;
            }
        INLINE constexpr static void WriteImpl(Stream&& out, From val) noexcept(
                noexcept(out.sputn(std::declval<const char*>(), sizeof(From)))) {
            if constexpr (endian != std::endian::native) {
                val = detail::byteswap(val);
            }
            alignas(alignof(From)) std::array<char, sizeof(From)> buffer
                    = detail::bit_cast<decltype(buffer)>(val);
            out.sputn(buffer.data(), sizeof(From));
        }

        template <contiguous_container Cont, std::unsigned_integral From>
        INLINE constexpr static void WriteImpl(Cont& out, From val) noexcept(
                noexcept(out.resize(std::declval<size_t>()))) {
            if constexpr (endian != std::endian::native) {
                val = detail::byteswap(val);
            }
            auto sz = out.size();
            out.resize(sz + sizeof(From));
            WriteImpl(std::addressof(out[sz]), val);
        }

        template <typename IterRef, std::unsigned_integral From>
            requires(byte_output_iterator<std::remove_cvref_t<IterRef>>)
        INLINE constexpr static void WriteImpl(IterRef&& out, From val) noexcept {
            // Both of these versions generate optimal code in GCC and
            // clang. I am splitting these cases because MSVC compiler does
            // cannot see through the iterator abstraction.
            using Iter = std::remove_cvref_t<IterRef>;
            if constexpr (contiguous_reverse_iterator<Iter>) {
                std::advance(out, sizeof(From));
                if constexpr (endian == std::endian::native) {
                    val = detail::byteswap(val);
                }
            } else {
                if constexpr (endian != std::endian::native) {
                    val = detail::byteswap(val);
                }
            }
            alignas(alignof(From)) std::array<char, sizeof(From)> buffer
                    = detail::bit_cast<decltype(buffer)>(val);
            if constexpr (
                    (std::contiguous_iterator<Iter>)
                    || (contiguous_reverse_iterator<Iter>)) {
                std::copy_n(buffer.data(), sizeof(From), std::to_address(out));
            } else {
                std::copy_n(buffer.data(), sizeof(From), out);
            }
            if constexpr ((std::forward_iterator<Iter>)&&(
                                  !contiguous_reverse_iterator<Iter>)) {
                std::advance(out, sizeof(From));
            }
        }

    public:
        template <typename Src>
        [[nodiscard]] INLINE constexpr static uint8_t Read1(Src&& in) noexcept(
                noexcept(ReadImpl<uint8_t>(std::forward<Src>(in)))) {
            return ReadImpl<uint8_t>(std::forward<Src>(in));
        }

        template <typename Src>
        [[nodiscard]] INLINE constexpr static uint16_t Read2(Src&& in) noexcept(
                noexcept(ReadImpl<uint16_t>(std::forward<Src>(in)))) {
            return ReadImpl<uint16_t>(std::forward<Src>(in));
        }

        template <typename Src>
        [[nodiscard]] INLINE constexpr static uint32_t Read4(Src&& in) noexcept(
                noexcept(ReadImpl<uint32_t>(std::forward<Src>(in)))) {
            return ReadImpl<uint32_t>(std::forward<Src>(in));
        }

        template <typename Src>
        [[nodiscard]] INLINE constexpr static uint64_t Read8(Src&& in) noexcept(
                noexcept(ReadImpl<uint64_t>(std::forward<Src>(in)))) {
            return ReadImpl<uint64_t>(std::forward<Src>(in));
        }

        template <size_t Size, typename Src>
        [[nodiscard]] INLINE constexpr static auto ReadN(Src&& in) noexcept(noexcept(
                ReadImpl<detail::select_unsigned_t<Size>>(std::forward<Src>(in)))) {
            return ReadImpl<detail::select_unsigned_t<Size>>(std::forward<Src>(in));
        }

        template <typename Src, std::integral To>
        INLINE constexpr static auto Read(Src&& in, To& val) noexcept(
                noexcept(ReadImpl<To>(std::forward<Src>(in)))) {
            val = ReadImpl<To>(std::forward<Src>(in));
        }

        template <std::unsigned_integral To, typename Src>
        [[nodiscard]] INLINE constexpr static auto Read(Src&& in) noexcept(
                noexcept(ReadImpl<To>(std::forward<Src>(in)))) {
            static_assert(
                    !std::same_as<std::remove_cvref_t<To>, bool>,
                    "Cannot portably use bool because sizeof(bool) is "
                    "implementation-defined");
            return ReadImpl<To>(std::forward<Src>(in));
        }

        template <std::signed_integral To, typename Src>
        [[nodiscard]] INLINE constexpr static auto Read(Src&& in) noexcept(
                noexcept(bit_cast<To>(
                        ReadImpl<std::make_unsigned_t<To>>(std::forward<Src>(in))))) {
            return bit_cast<To>(
                    ReadImpl<std::make_unsigned_t<To>>(std::forward<Src>(in)));
        }

        template <typename Dst>
        INLINE constexpr static void Write1(Dst&& out, uint8_t val) noexcept(
                noexcept(WriteImpl(std::forward<Dst>(out), val))) {
            WriteImpl(std::forward<Dst>(out), val);
        }

        template <typename Dst>
        INLINE constexpr static void Write2(Dst&& out, uint16_t val) noexcept(
                noexcept(WriteImpl(std::forward<Dst>(out), val))) {
            WriteImpl(std::forward<Dst>(out), val);
        }

        template <typename Dst>
        INLINE constexpr static void Write4(Dst&& out, uint32_t val) noexcept(
                noexcept(WriteImpl(std::forward<Dst>(out), val))) {
            WriteImpl(std::forward<Dst>(out), val);
        }

        template <typename Dst>
        INLINE constexpr static void Write8(Dst&& out, uint64_t val) noexcept(
                noexcept(WriteImpl(std::forward<Dst>(out), val))) {
            WriteImpl(std::forward<Dst>(out), val);
        }

        template <
                size_t Size, typename Dst,
                typename Uint_t = detail::select_unsigned_t<Size>>
        INLINE constexpr static void WriteN(Dst&& out, Uint_t val) noexcept(
                noexcept(WriteImpl(std::forward<Dst>(out), val))) {
            WriteImpl(std::forward<Dst>(out), val);
        }

        template <typename Dst, std::unsigned_integral From>
        INLINE constexpr static auto Write(Dst&& out, From val) noexcept(
                noexcept(WriteImpl(std::forward<Dst>(out), val))) {
            static_assert(
                    !std::same_as<std::remove_cvref_t<From>, bool>,
                    "Cannot portably use bool because sizeof(bool) is "
                    "implementation-defined");
            WriteImpl(std::forward<Dst>(out), val);
        }

        template <typename Dst, std::signed_integral From>
        INLINE constexpr static auto Write(Dst&& out, From val) noexcept(
                noexcept(WriteImpl(
                        std::forward<Dst>(out),
                        bit_cast<std::make_unsigned_t<From>>(val)))) {
            WriteImpl(std::forward<Dst>(out), bit_cast<std::make_unsigned_t<From>>(val));
        }
    };

    CONST_INLINE constexpr auto reverse(std::endian endian) noexcept {
        if (endian == std::endian::big) {
            return std::endian::little;
        }
        return std::endian::big;
    }
}    // namespace detail

template <typename Src>
INLINE constexpr uint8_t Read1(Src& in) noexcept(
        noexcept(detail::EndianBase<std::endian::native>::Read<uint8_t>(in))) {
    return detail::EndianBase<std::endian::native>::Read<uint8_t>(in);
}

template <typename Dst>
INLINE constexpr void Write1(Dst& out, uint8_t const val) noexcept(
        noexcept(detail::EndianBase<std::endian::native>::Write(out, val))) {
    detail::EndianBase<std::endian::native>::Write(out, val);
}

using SourceEndian  = detail::EndianBase<std::endian::native>;
using ReverseEndian = detail::EndianBase<detail::reverse(std::endian::native)>;
using BigEndian     = detail::EndianBase<std::endian::big>;
using LittleEndian  = detail::EndianBase<std::endian::little>;

#undef INLINE
#undef CONST_INLINE
// #undef PURE_INLINE

#endif    // LIB_BIGENDIAN_IO_HH
