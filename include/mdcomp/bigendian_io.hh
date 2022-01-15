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
#    define INLINE __forceinline
#else
#    define INLINE inline
#endif

namespace detail {
    // Meta-programming stuff.

    template <typename Iter>
    concept byte_input_iterator = requires() {
        std::input_iterator<Iter>;
        (std::is_same_v<std::iter_value_t<Iter>, char>)
                || (std::is_same_v<std::iter_value_t<Iter>, uint8_t>);
    };

    template <typename Iter>
    concept byte_output_iterator = (std::output_iterator<Iter, uint8_t>)
                                   || (std::output_iterator<Iter, char>);

    template <typename T>
    concept container = requires(T a, const T b) {
        std::regular<T>;
        std::swappable<T>;
        std::destructible<typename T::value_type>;
        std::same_as<typename T::reference, typename T::value_type&>;
        std::same_as<
                typename T::const_reference, const typename T::value_type&>;
        std::forward_iterator<typename T::iterator>;
        std::forward_iterator<typename T::const_iterator>;
        std::signed_integral<typename T::difference_type>;
        std::same_as<
                typename T::difference_type,
                typename std::iterator_traits<
                        typename T::iterator>::difference_type>;
        std::same_as<
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
        std::contiguous_iterator<typename T::iterator>;
        std::contiguous_iterator<typename T::const_iterator>;
        { a.resize(n) } -> std::same_as<void>;
        {*a.data() = v};
    };

    template <size_t Size>
    constexpr inline auto select_unsigned() {
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
    constexpr const inline bool is_reverse_iterator_v
            = is_reverse_iterator<T>::value;

    template <typename T>
    concept contiguous_reverse_iterator = requires() {
        is_reverse_iterator_v<T>;
        std::contiguous_iterator<typename T::iterator_type>;
    };

    // Mashed together implementation based on libstdc++/libc++/MS STL.
    // GCC/clang both have a 128-bit integer type, which this implementation
    // supports; but MSVC compiler does not support a 128-bit integer, so this
    // is not portable.
    template <std::integral T>
    [[nodiscard, gnu::const, gnu::always_inline]] INLINE constexpr T byteswap(
            T val) noexcept {
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
        size_t diff  = CHAR_BIT * (sizeof(T) - 1);
        uint_t mask1 = static_cast<unsigned char>(~0U);
        auto   mask2 = static_cast<uint_t>(mask1 << diff);
        uint_t value = val;
        for (size_t ii = 0; ii < sizeof(T) / 2; ++ii) {
            uint_t byte1 = value & mask1;
            uint_t byte2 = value & mask2;
            value        = static_cast<uint_t>(
                    value ^ byte1 ^ byte2 ^ (byte1 << diff) ^ (byte2 >> diff));
            mask1 = static_cast<uint_t>(mask1 << CHAR_BIT);
            mask2 = static_cast<uint_t>(mask2 >> CHAR_BIT);
            diff -= 2ULL * CHAR_BIT;
        }
        return value;
    }

    template <typename To, typename From>
        requires requires() {
            sizeof(To) == sizeof(From);
            std::is_trivially_copyable_v<To>;
            std::is_trivially_copyable_v<From>;
        }
    [[nodiscard, gnu::const, gnu::always_inline]] INLINE constexpr To bit_cast(
            const From& from) noexcept {
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
        [[nodiscard, gnu::always_inline]] INLINE static To ReadImpl(
                Stream&& in) noexcept {
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
        [[nodiscard, gnu::always_inline]] INLINE static To ReadImpl(
                Stream&& in) noexcept {
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
        [[nodiscard, gnu::always_inline]] INLINE static To ReadImpl(
                IterRef&& in) noexcept {
            // Both of these versions generate optimal code in GCC and
            // clang. I am splitting these cases because MSVC compiler does
            // not understand memmove (which is called my std::copy which is
            // called by std::copy_n).
            using Iter = std::remove_cvref_t<IterRef>;
            if constexpr (contiguous_reverse_iterator<Iter>) {
                std::advance(in, sizeof(To));
            }
            const To val = [&]() {
                alignas(alignof(To)) std::array<char, sizeof(To)> buffer;
                if constexpr (
                        (std::contiguous_iterator<Iter>)
                        || (contiguous_reverse_iterator<Iter>)) {
                    std::memcpy(buffer.data(), std::to_address(in), sizeof(To));
                } else {
                    std::copy_n(in, sizeof(To), std::begin(buffer));
                }
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
        [[gnu::always_inline]] INLINE static void WriteImpl(
                Stream&& out, From val) noexcept {
            if constexpr (endian != std::endian::native) {
                val = detail::byteswap(val);
            }
            alignas(alignof(From)) std::array<char, sizeof(From)> buffer;
            std::memcpy(buffer.data(), &val, sizeof(From));
            out.write(buffer.data(), sizeof(From));
        }

        template <std::unsigned_integral From, typename Stream>
            requires requires(Stream s, const char* p, std::streamsize c) {
                { s.sputn(p, c) } -> std::same_as<std::streamsize>;
            }
        [[gnu::always_inline]] INLINE static void WriteImpl(
                Stream&& out, From val) noexcept {
            if constexpr (endian != std::endian::native) {
                val = detail::byteswap(val);
            }
            alignas(alignof(From)) std::array<char, sizeof(From)> buffer;
            std::memcpy(buffer.data(), &val, sizeof(From));
            out.sputn(buffer.data(), sizeof(From));
        }

        template <contiguous_container Cont, std::unsigned_integral From>
        [[gnu::always_inline]] INLINE static void WriteImpl(
                Cont& out, From val) noexcept {
            if constexpr (endian != std::endian::native) {
                val = detail::byteswap(val);
            }
            auto sz = out.size();
            out.resize(sz + sizeof(From));
            WriteImpl(std::addressof(out[sz]), val);
        }

        template <typename IterRef, std::unsigned_integral From>
            requires(byte_output_iterator<std::remove_cvref_t<IterRef>>)
        [[gnu::always_inline]] INLINE static void WriteImpl(
                IterRef&& out, From val) noexcept {
            // Both of these versions generate optimal code in GCC and
            // clang. I am splitting these cases because MSVC compiler does
            // not understand memmove (which is called my std::copy which is
            // called by std::copy_n).
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
            if constexpr (
                    (std::contiguous_iterator<Iter>)
                    || (contiguous_reverse_iterator<Iter>)) {
                std::memcpy(std::to_address(out), &val, sizeof(From));
            } else {
                alignas(alignof(From)) std::array<char, sizeof(From)> buffer;
                std::memcpy(buffer.data(), &val, sizeof(From));
                std::copy_n(buffer.data(), sizeof(From), out);
            }
            if constexpr ((std::forward_iterator<Iter>)&&(
                                  !contiguous_reverse_iterator<Iter>)) {
                std::advance(out, sizeof(From));
            }
        }

    public:
        template <typename Src>
        [[nodiscard, gnu::always_inline]] INLINE static uint8_t Read1(
                Src&& in) noexcept {
            return ReadImpl<uint8_t>(std::forward<Src>(in));
        }

        template <typename Src>
        [[nodiscard, gnu::always_inline]] INLINE static uint16_t Read2(
                Src&& in) noexcept {
            return ReadImpl<uint16_t>(std::forward<Src>(in));
        }

        template <typename Src>
        [[nodiscard, gnu::always_inline]] INLINE static uint32_t Read4(
                Src&& in) noexcept {
            return ReadImpl<uint32_t>(std::forward<Src>(in));
        }

        template <typename Src>
        [[nodiscard, gnu::always_inline]] INLINE static uint64_t Read8(
                Src&& in) noexcept {
            return ReadImpl<uint64_t>(std::forward<Src>(in));
        }

        template <size_t Size, typename Src>
        [[nodiscard, gnu::always_inline]] INLINE static auto ReadN(
                Src&& in) noexcept -> detail::select_unsigned_t<Size> {
            using uint_t = detail::select_unsigned_t<Size>;
            return ReadImpl<uint_t>(std::forward<Src>(in));
        }

        template <typename Src, std::integral To>
        [[gnu::always_inline]] INLINE static auto Read(
                Src&& in, To& val) noexcept {
            val = ReadImpl<To>(std::forward<Src>(in));
        }

        template <std::unsigned_integral To, typename Src>
        [[nodiscard, gnu::always_inline]] INLINE static auto Read(
                Src&& in) noexcept {
            static_assert(
                    !std::same_as<std::remove_cvref_t<To>, bool>,
                    "Cannot portably use bool because sizeof(bool) is "
                    "implementation-defined");
            return ReadImpl<To>(std::forward<Src>(in));
        }

        template <std::signed_integral To, typename Src>
        [[nodiscard, gnu::always_inline]] INLINE static auto Read(
                Src&& in) noexcept {
            using uint_t = std::make_unsigned_t<To>;
            return bit_cast<To>(ReadImpl<uint_t>(std::forward<Src>(in)));
        }

        template <typename Dst>
        [[gnu::always_inline]] INLINE static void Write1(
                Dst&& out, uint8_t val) noexcept {
            WriteImpl(std::forward<Dst>(out), val);
        }

        template <typename Dst>
        [[gnu::always_inline]] INLINE static void Write2(
                Dst&& out, uint16_t val) noexcept {
            WriteImpl(std::forward<Dst>(out), val);
        }

        template <typename Dst>
        [[gnu::always_inline]] INLINE static void Write4(
                Dst&& out, uint32_t val) noexcept {
            WriteImpl(std::forward<Dst>(out), val);
        }

        template <typename Dst>
        [[gnu::always_inline]] INLINE static void Write8(
                Dst&& out, uint64_t val) noexcept {
            WriteImpl(std::forward<Dst>(out), val);
        }

        template <
                size_t Size, typename Dst,
                typename Uint_t = detail::select_unsigned_t<Size>>
        [[gnu::always_inline]] INLINE static void WriteN(
                Dst&& out, Uint_t val) noexcept {
            WriteImpl(std::forward<Dst>(out), val);
        }

        template <typename Dst, std::unsigned_integral From>
        [[gnu::always_inline]] INLINE static auto Write(
                Dst&& out, From val) noexcept {
            static_assert(
                    !std::same_as<std::remove_cvref_t<From>, bool>,
                    "Cannot portably use bool because sizeof(bool) is "
                    "implementation-defined");
            WriteImpl(std::forward<Dst>(out), val);
        }

        template <typename Dst, std::signed_integral From>
        [[gnu::always_inline]] INLINE static auto Write(
                Dst&& out, From val) noexcept {
            using uint_t = std::make_unsigned_t<From>;
            WriteImpl(std::forward<Dst>(out), bit_cast<uint_t>(val));
        }
    };

    [[gnu::const, gnu::always_inline]] INLINE constexpr auto reverse(
            std::endian endian) {
        if (endian == std::endian::big) {
            return std::endian::little;
        }
        return std::endian::big;
    }
}    // namespace detail

template <typename Src>
[[gnu::always_inline]] INLINE uint8_t Read1(Src& in) noexcept {
    return detail::EndianBase<std::endian::native>::Read<uint8_t>(in);
}

template <typename Dst>
[[gnu::always_inline]] INLINE void Write1(
        Dst& out, uint8_t const val) noexcept {
    detail::EndianBase<std::endian::native>::Write(out, val);
}

using SourceEndian  = detail::EndianBase<std::endian::native>;
using ReverseEndian = detail::EndianBase<detail::reverse(std::endian::native)>;
using BigEndian     = detail::EndianBase<std::endian::big>;
using LittleEndian  = detail::EndianBase<std::endian::little>;

#undef INLINE

#endif    // LIB_BIGENDIAN_IO_HH
