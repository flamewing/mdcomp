/*
 * Copyright (C) Flamewing 2011-2025 <flamewing.sonic@gmail.com>
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

#include "ignore_unused_variable_warning.hh"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <concepts>    // IWYU pragma: keep
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <type_traits>
#include <utility>

#if defined(__SIZEOF_INT128__)
#    define MDCOMP_HAS_INT128 1
#elif defined(_MSVC_STL_VERSION) && __has_include(<__msvc_int128.hpp>)
// MSVC has no native 128-bit scalar. Its STL supplies integer-class equivalents.
#    include <__msvc_int128.hpp>
#    define MDCOMP_HAS_INT128 1
#else
#    define MDCOMP_HAS_INT128 0
#endif

#ifdef _MSC_VER
#    include <stdlib.h>
#    pragma intrinsic(_byteswap_ushort)
#    pragma intrinsic(_byteswap_ulong)
#    pragma intrinsic(_byteswap_uint64)
#elif defined(__INTEL_COMPILER)
#    include <byteswap.h>
#endif

#if defined(_MSC_VER)
#    define INLINE       [[msvc::forceinline]] inline
#    define CONST_INLINE [[msvc::forceinline]] inline
#    define PURE_INLINE  [[msvc::forceinline]] inline
#elif defined(__GNUG__)
#    define INLINE       [[gnu::always_inline]] inline
#    define CONST_INLINE [[using gnu: const, always_inline]] inline
#    define PURE_INLINE  [[using gnu: pure, always_inline]] inline
#else
#    define INLINE       inline
#    define CONST_INLINE inline
#    define PURE_INLINE  inline
#endif

#if defined(_MSC_VER) && !defined(__has_builtin)
#    define __has_builtin(x) 0
#endif

namespace utils {
    // Meta-programming stuff.

#if defined(__SIZEOF_INT128__)
    using int128_t  = __int128_t;
    using uint128_t = __uint128_t;
#elif MDCOMP_HAS_INT128
    using int128_t  = std::_Signed128;
    using uint128_t = std::_Unsigned128;
#endif

    template <typename T>
    concept integral = std::integral<T>
#if MDCOMP_HAS_INT128
                       || std::same_as<std::remove_cv_t<T>, int128_t>
                       || std::same_as<std::remove_cv_t<T>, uint128_t>
#endif
            ;

    template <typename T>
    concept signed_integral = std::signed_integral<T>
#if MDCOMP_HAS_INT128
                              || std::same_as<std::remove_cv_t<T>, int128_t>
#endif
            ;

    template <typename T>
    concept unsigned_integral = integral<T> && !signed_integral<T>;

    template <typename T>
    concept byte = std::same_as<std::remove_cv_t<T>, char>
                   || std::same_as<std::remove_cv_t<T>, signed char>
                   || std::same_as<std::remove_cv_t<T>, unsigned char>;

    namespace detail {
        template <typename Iter, typename = void>
        struct output_value {};

        template <typename Iter>
        struct output_value<Iter, std::void_t<std::iter_value_t<Iter>>> {
            using type = std::iter_value_t<Iter>;
        };

        // Standard output-only iterators expose void as their value_type.
        template <typename Cont>
        struct output_value<std::back_insert_iterator<Cont>> {
            using type = typename Cont::value_type;
        };

        template <typename Cont>
        struct output_value<std::front_insert_iterator<Cont>> {
            using type = typename Cont::value_type;
        };

        template <typename Cont>
        struct output_value<std::insert_iterator<Cont>> {
            using type = typename Cont::value_type;
        };

        template <typename T, typename Char, typename Traits>
        struct output_value<std::ostream_iterator<T, Char, Traits>> {
            using type = T;
        };

        template <typename Char, typename Traits>
        struct output_value<std::ostreambuf_iterator<Char, Traits>> {
            using type = Char;
        };
    }    // namespace detail

    template <typename Iter>
    concept byte_input_iterator = requires() {
        requires std::input_iterator<Iter>;
        requires byte<std::iter_value_t<Iter>>;
    };

    template <typename Iter>
    concept byte_output_iterator = std::output_iterator<Iter, unsigned char> && requires {
        typename detail::output_value<Iter>::type;
        requires byte<typename detail::output_value<Iter>::type>;
    };

    template <typename T>
    concept container = std::regular<T> && std::swappable<T>
                        && std::destructible<typename T::value_type>
                        && std::ranges::forward_range<T>;

    template <typename T>
    concept contiguous_container = container<T> && std::ranges::contiguous_range<T>
                                   && std::ranges::sized_range<T>
                                   && requires(
                                           T mut_container, typename T::size_type count,
                                           typename T::value_type value) {
                                          {
                                              mut_container.resize(count)
                                          } -> std::same_as<void>;
                                          { *mut_container.data() = value };
                                      };

    namespace detail {
        template <size_t Size>
        CONST_INLINE constexpr auto uint_for_size() noexcept {
            static_assert(std::has_single_bit(Size), "Size must be a power of 2");
#if MDCOMP_HAS_INT128
            static_assert(
                    Size > 0 && Size <= sizeof(uint128_t),
                    "Size must be between 1 and sizeof(uint128_t)");
#else
            static_assert(
                    Size > 0 && Size <= sizeof(uint64_t),
                    "Size must be between 1 and sizeof(uint64_t)");
#endif
            if constexpr (Size == sizeof(uint8_t)) {
                return uint8_t{};
            } else if constexpr (Size == sizeof(uint16_t)) {
                return uint16_t{};
            } else if constexpr (Size == sizeof(uint32_t)) {
                return uint32_t{};
            }
#if MDCOMP_HAS_INT128
            else if constexpr (Size == sizeof(uint128_t)) {
                return uint128_t{};
            }
#endif
            else {
                return uint64_t{};
            }
        }

        template <size_t Size>
        using uint_for_size_t = decltype(uint_for_size<Size>());

        template <typename T>
        using uint_for_t = decltype(uint_for_size<sizeof(T)>());

        // Only reverse iterators directly wrapping contiguous iterators use the
        // contiguous fast path; nested adaptors use ordinary iterator traversal.
        template <typename T>
        struct is_reverse_iterator : std::false_type {};

        template <typename T>
        struct is_reverse_iterator<std::reverse_iterator<T>> : std::true_type {};

        template <typename T>
        struct is_reverse_iterator<T const> : is_reverse_iterator<T> {};

        template <typename T>
        struct is_reverse_iterator<T&> : is_reverse_iterator<T> {};

        template <typename T>
        constexpr inline bool const is_reverse_iterator_v = is_reverse_iterator<T>::value;

        template <typename T>
        concept contiguous_reverse_iterator
                = is_reverse_iterator_v<T>
                  && std::contiguous_iterator<typename T::iterator_type>;

        template <typename Iter>
        consteval bool nothrow_byte_read() {
            if constexpr (contiguous_reverse_iterator<Iter>) {
                return noexcept(std::declval<Iter&>() += std::iter_difference_t<Iter>{})
                       && noexcept(std::to_address(std::declval<Iter&>().base()))
                       && noexcept(static_cast<unsigned char>(
                               *std::to_address(std::declval<Iter&>().base())));
            } else if constexpr (std::contiguous_iterator<Iter>) {
                return noexcept(std::declval<Iter&>() += std::iter_difference_t<Iter>{})
                       && noexcept(std::to_address(std::declval<Iter&>()))
                       && noexcept(static_cast<unsigned char>(
                               *std::to_address(std::declval<Iter&>())));
            } else {
                return noexcept(static_cast<unsigned char>(*std::declval<Iter&>()))
                       && noexcept(++std::declval<Iter&>());
            }
        }

        template <typename Iter>
        consteval bool nothrow_byte_write() {
            if constexpr (contiguous_reverse_iterator<Iter>) {
                return noexcept(std::declval<Iter&>() += std::iter_difference_t<Iter>{})
                       && noexcept(std::to_address(std::declval<Iter&>().base()))
                       && noexcept(
                               *std::to_address(std::declval<Iter&>().base())
                               = std::declval<unsigned char>());
            } else if constexpr (std::contiguous_iterator<Iter>) {
                return noexcept(std::declval<Iter&>() += std::iter_difference_t<Iter>{})
                       && noexcept(std::to_address(std::declval<Iter&>()))
                       && noexcept(
                               *std::to_address(std::declval<Iter&>())
                               = std::declval<unsigned char>());
            } else {
                return noexcept(*std::declval<Iter&>() = std::declval<unsigned char>())
                       && noexcept(++std::declval<Iter&>());
            }
        }

        template <typename Stream>
        concept has_read_bytes_function
                = requires(Stream&& stream, char* pointer, std::streamsize count) {
                      {
                          std::forward<Stream>(stream).read(pointer, count)
                      } -> std::common_reference_with<Stream>;
                  };

        template <typename Stream>
        concept is_input_streambuf
                = requires(Stream stream, char* pointer, std::streamsize count) {
                      { stream.sgetn(pointer, count) } -> std::same_as<std::streamsize>;
                  };

        template <typename Stream>
        concept has_write_bytes_function
                = requires(Stream&& stream, char const* pointer, std::streamsize count) {
                      {
                          std::forward<Stream>(stream).write(pointer, count)
                      } -> std::common_reference_with<Stream>;
                  };

        template <typename Stream>
        concept is_output_streambuf
                = requires(Stream stream, char const* pointer, std::streamsize count) {
                      { stream.sputn(pointer, count) } -> std::same_as<std::streamsize>;
                  };
    }    // namespace detail

    template <class... Ts>
    struct overloaded : public Ts... {
        using Ts::operator()...;

        constexpr explicit overloaded(Ts... callables) : Ts(callables)... {}
    };
    template <class... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;

    template <unsigned_integral T>
    [[nodiscard]] CONST_INLINE constexpr T fallback_byteswap(T value) noexcept {
        if constexpr (sizeof(T) == 1) {
            return value;    // No-op for 8-bit values.
        } else {
            // Avoid standard integer traits/rotations: strict-mode __int128 and
            // MSVC's integer classes are not necessarily standard integral types.
            T result{};
            for (size_t ii = 0; ii < sizeof(T); ++ii) {
                result = static_cast<T>(
                        (result << CHAR_BIT) | (value & static_cast<T>(UCHAR_MAX)));
                value >>= CHAR_BIT;
            }
            return result;
        }
    }

    static_assert(fallback_byteswap(uint8_t{0x35U}) == uint8_t{0x35U});
    static_assert(fallback_byteswap(uint16_t{0x1357U}) == uint16_t{0x5713U});
    static_assert(fallback_byteswap(0x01234567U) == 0x67452301U);
    static_assert(fallback_byteswap(0x0123456789abcdefULL) == 0xefcdab8967452301ULL);

    // Mashed together implementation based on libstdc++/libc++/MS STL.
    // Use native 128-bit integers or MSVC STL integer classes when available.
    template <unsigned_integral T>
    [[nodiscard]] CONST_INLINE constexpr T byteswap_impl(T value) noexcept {
#if defined(__cpp_lib_byteswap) && __cpp_lib_byteswap >= 202110L
        if constexpr (std::unsigned_integral<T>) {
            return std::byteswap(value);
        } else {
            return fallback_byteswap(value);
        }
#else
        if constexpr (CHAR_BIT == 8) {
            if (!std::is_constant_evaluated()) {
                constexpr auto const builtin_bswap = overloaded(
                        [](uint8_t const val) {
                            return val;    // No-op for 8-bit values.
                        },
#    ifdef __GNUG__
                        [](uint16_t const val) {
                            return __builtin_bswap16(val);
                        },
                        [](uint32_t const val) {
                            return __builtin_bswap32(val);
                        },
                        [](uint64_t const val) {
                            return __builtin_bswap64(val);
                        },
#    elif defined(_MSC_VER)
                        [](uint16_t const val) {
                            return _byteswap_ushort(val);
                        },
                        [](uint32_t const val) {
                            return _byteswap_ulong(val);
                        },
                        [](uint64_t const val) {
                            return _byteswap_uint64(val);
                        },
#    endif
                        [](auto const& val) {
                            return fallback_byteswap(val);
                        });
                if constexpr (sizeof(T) == 16) {
#    if defined(__SIZEOF_INT128__) && __has_builtin(__builtin_bswap128)
                    return __builtin_bswap128(value);
#    else
                    T high = builtin_bswap(static_cast<uint64_t>(value >> 64U));
                    T low  = builtin_bswap(static_cast<uint64_t>(value));
                    return high | (low << 64U);
#    endif
                } else {
                    return builtin_bswap(value);
                }
            } else {
                return fallback_byteswap(value);
            }
        } else {
            return fallback_byteswap(value);
        }
#endif
    }

    static_assert(byteswap_impl(uint8_t{0x35U}) == uint8_t{0x35U});
    static_assert(byteswap_impl(uint16_t{0x1357U}) == uint16_t{0x5713U});
    static_assert(byteswap_impl(0x01234567U) == 0x67452301U);
    static_assert(byteswap_impl(0x0123456789abcdefULL) == 0xefcdab8967452301ULL);

    template <integral T>
    requires(!std::same_as<std::remove_cv_t<T>, bool>)
    [[nodiscard]] CONST_INLINE constexpr T byteswap(T value) noexcept {
        // Need this to handle "(unsigned)? long long" and "(unsigned)? long".
        // They can be both 64-bit depending on platform, and which one is used
        // in the definition of uint64_t, the other will not match.
        using uint_t = detail::uint_for_t<T>;
        return std::bit_cast<T>(byteswap_impl(std::bit_cast<uint_t>(value)));
    }

    template <std::endian endian>
    struct endian_base {
    private:
        template <unsigned_integral To, unsigned_integral From>
        requires(std::same_as<std::remove_cvref_t<From>, std::remove_cvref_t<To>>)
        [[nodiscard]] INLINE constexpr static To read_impl(From const input) noexcept {
            if constexpr (endian != std::endian::native) {
                return byteswap(input);
            } else {
                return input;
            }
        }

        template <unsigned_integral To, typename Stream>
        requires detail::has_read_bytes_function<Stream>
        [[nodiscard]] INLINE constexpr static To read_impl(Stream&& input) noexcept(
                noexcept(std::forward<Stream>(input).read(
                        std::declval<char*>(), sizeof(To)))) {
            alignas(alignof(To)) std::array<char, sizeof(To)> buffer{};
            std::forward<Stream>(input).read(buffer.data(), sizeof(To));
            if constexpr (endian != std::endian::native) {
                return byteswap(std::bit_cast<To>(buffer));
            } else {
                return std::bit_cast<To>(buffer);
            }
        }

        template <unsigned_integral To, typename Stream>
        requires detail::is_input_streambuf<Stream>
        [[nodiscard]] INLINE constexpr static To read_impl(Stream&& input) {
            alignas(alignof(To)) std::array<char, sizeof(To)> buffer{};
            // streambuf has no state flags: report short transfers explicitly.
            // Bytes already transferred are consumed; no rollback is attempted.
            if (std::forward<Stream>(input).sgetn(buffer.data(), sizeof(To))
                != sizeof(To)) {
                throw std::ios_base::failure("Short endian streambuf read");
            }
            if constexpr (endian != std::endian::native) {
                return byteswap(std::bit_cast<To>(buffer));
            } else {
                return std::bit_cast<To>(buffer);
            }
        }

        template <unsigned_integral To, typename IterRef>
        requires(byte_input_iterator<std::remove_cvref_t<IterRef>>)
        // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
        [[nodiscard]] INLINE constexpr static To read_impl(IterRef&& input) noexcept(
                detail::nothrow_byte_read<std::remove_cvref_t<IterRef>>()) {
            using iterator = std::remove_cvref_t<IterRef>;
            alignas(alignof(To)) std::array<unsigned char, sizeof(To)> buffer{};
            if constexpr (detail::contiguous_reverse_iterator<iterator>) {
                input += sizeof(To);
            }
            // Both of these versions generate optimal code in GCC and
            // clang. I am splitting these cases because MSVC compiler does
            // cannot see through the iterator abstraction.
            if constexpr (detail::contiguous_reverse_iterator<iterator>) {
                std::ranges::copy_n(
                        std::to_address(input.base()), sizeof(To), buffer.data());
            } else if constexpr (std::contiguous_iterator<iterator>) {
                std::ranges::copy_n(std::to_address(input), sizeof(To), buffer.data());
                input += sizeof(To);
            } else {
                for (auto& byte : buffer) {
                    byte = static_cast<unsigned char>(*input);
                    ++input;
                }
            }
            To const value = [&]() {
                if constexpr (detail::contiguous_reverse_iterator<iterator>) {
                    if constexpr (endian == std::endian::native) {
                        return byteswap(std::bit_cast<To>(buffer));
                    } else {
                        return std::bit_cast<To>(buffer);
                    }
                } else {
                    if constexpr (endian != std::endian::native) {
                        return byteswap(std::bit_cast<To>(buffer));
                    } else {
                        return std::bit_cast<To>(buffer);
                    }
                }
            }();
            return value;
        }

        template <unsigned_integral T>
        INLINE constexpr static void write_impl(T& input, T value) noexcept {
            if constexpr (endian != std::endian::native) {
                input = byteswap(value);
            } else {
                input = value;
            }
        }

        template <unsigned_integral From, typename Stream>
        requires detail::has_write_bytes_function<Stream>
        INLINE constexpr static void write_impl(Stream&& output, From value) noexcept(
                noexcept(std::forward<Stream>(output).write(
                        std::declval<char const*>(), sizeof(From)))) {
            using buffer_t = std::array<char, sizeof(From)>;
            if constexpr (endian != std::endian::native) {
                value = byteswap(value);
            }
            alignas(alignof(From)) auto buffer = std::bit_cast<buffer_t>(value);
            std::forward<Stream>(output).write(buffer.data(), sizeof(From));
        }

        template <unsigned_integral From, typename Stream>
        requires detail::is_output_streambuf<Stream>
        INLINE constexpr static void write_impl(Stream&& output, From value) {
            using buffer_t = std::array<char, sizeof(From)>;
            if constexpr (endian != std::endian::native) {
                value = byteswap(value);
            }
            alignas(alignof(From)) auto buffer = std::bit_cast<buffer_t>(value);
            if (std::forward<Stream>(output).sputn(buffer.data(), sizeof(From))
                != sizeof(From)) {
                throw std::ios_base::failure("Short endian streambuf write");
            }
        }

        template <contiguous_container Cont, unsigned_integral From>
        requires byte<typename Cont::value_type>
        INLINE constexpr static void write_impl(Cont& output, From value) noexcept(
                noexcept(output.size()) && noexcept(output.resize(std::declval<size_t>()))
                && noexcept(output.data())
                && noexcept(write_impl(output.data(), value))) {
            auto const size = output.size();
            output.resize(size + sizeof(From));
            auto* destination = output.data() + size;
            write_impl(destination, value);
        }

        template <typename IterRef, unsigned_integral From>
        requires(byte_output_iterator<std::remove_cvref_t<IterRef>>)
        // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
        INLINE constexpr static void write_impl(IterRef&& output, From value) noexcept(
                detail::nothrow_byte_write<std::remove_cvref_t<IterRef>>()) {
            // Both of these versions generate optimal code in GCC and
            // clang. I am splitting these cases because MSVC compiler does
            // cannot see through the iterator abstraction.
            using iterator = std::remove_cvref_t<IterRef>;
            using buffer_t = std::array<unsigned char, sizeof(From)>;
            if constexpr (detail::contiguous_reverse_iterator<iterator>) {
                output += sizeof(From);
                if constexpr (endian == std::endian::native) {
                    value = byteswap(value);
                }
            } else {
                if constexpr (endian != std::endian::native) {
                    value = byteswap(value);
                }
            }
            alignas(alignof(From)) auto buffer = std::bit_cast<buffer_t>(value);
            if constexpr (detail::contiguous_reverse_iterator<iterator>) {
                std::ranges::copy(buffer, std::to_address(output.base()));
            } else if constexpr (std::contiguous_iterator<iterator>) {
                std::ranges::copy(buffer, std::to_address(output));
                output += sizeof(From);
            } else {
                for (auto byte : buffer) {
                    *output = byte;
                    ++output;
                }
            }
        }

        template <typename Dst, typename From>
        constexpr static bool writable_to = requires(Dst&& output, From value) {
            write_impl(std::forward<Dst>(output), value);
        };

    public:
        template <typename Src>
        [[nodiscard]] INLINE constexpr static uint8_t read1(Src&& input) noexcept(
                noexcept(read_impl<uint8_t>(std::forward<Src>(input)))) {
            return read_impl<uint8_t>(std::forward<Src>(input));
        }

        template <typename Src>
        [[nodiscard]] INLINE constexpr static uint16_t read2(Src&& input) noexcept(
                noexcept(read_impl<uint16_t>(std::forward<Src>(input)))) {
            return read_impl<uint16_t>(std::forward<Src>(input));
        }

        template <typename Src>
        [[nodiscard]] INLINE constexpr static uint32_t read4(Src&& input) noexcept(
                noexcept(read_impl<uint32_t>(std::forward<Src>(input)))) {
            return read_impl<uint32_t>(std::forward<Src>(input));
        }

        template <typename Src>
        [[nodiscard]] INLINE constexpr static uint64_t read8(Src&& input) noexcept(
                noexcept(read_impl<uint64_t>(std::forward<Src>(input)))) {
            return read_impl<uint64_t>(std::forward<Src>(input));
        }

#if MDCOMP_HAS_INT128
        template <typename Src>
        [[nodiscard]] INLINE constexpr static uint128_t read16(Src&& input) noexcept(
                noexcept(read_impl<uint128_t>(std::forward<Src>(input)))) {
            return read_impl<uint128_t>(std::forward<Src>(input));
        }
#endif

        template <size_t Size, typename Src>
        [[nodiscard]] INLINE constexpr static auto read_n(Src&& input) noexcept(noexcept(
                read_impl<detail::uint_for_size_t<Size>>(std::forward<Src>(input)))) {
            return read_impl<detail::uint_for_size_t<Size>>(std::forward<Src>(input));
        }

        template <typename Src, integral To>
        requires(!std::same_as<To, bool>)
        INLINE constexpr static auto read(Src&& input, To& value) noexcept(
                noexcept(read<To>(std::forward<Src>(input)))) {
            value = read<To>(std::forward<Src>(input));
        }

        template <unsigned_integral To, typename Src>
        requires(!std::same_as<std::remove_cv_t<To>, bool>)
        [[nodiscard]] INLINE constexpr static auto read(Src&& input) noexcept(
                noexcept(read_impl<To>(std::forward<Src>(input)))) {
            static_assert(
                    !std::same_as<std::remove_cvref_t<To>, bool>,
                    "Cannot portably use bool because sizeof(bool) is "
                    "implementation-defined");
            return read_impl<To>(std::forward<Src>(input));
        }

        template <signed_integral To, typename Src>
        [[nodiscard]] INLINE constexpr static auto read(Src&& input) noexcept(
                noexcept(std::bit_cast<To>(
                        read_impl<detail::uint_for_t<To>>(std::forward<Src>(input))))) {
            return std::bit_cast<To>(
                    read_impl<detail::uint_for_t<To>>(std::forward<Src>(input)));
        }

        template <typename Dst>
        requires writable_to<Dst, uint8_t>
        INLINE constexpr static void write1(Dst&& output, uint8_t value) noexcept(
                noexcept(write_impl(std::forward<Dst>(output), value))) {
            write_impl(std::forward<Dst>(output), value);
        }

        template <typename Dst>
        requires writable_to<Dst, uint16_t>
        INLINE constexpr static void write2(Dst&& output, uint16_t value) noexcept(
                noexcept(write_impl(std::forward<Dst>(output), value))) {
            write_impl(std::forward<Dst>(output), value);
        }

        template <typename Dst>
        requires writable_to<Dst, uint32_t>
        INLINE constexpr static void write4(Dst&& output, uint32_t value) noexcept(
                noexcept(write_impl(std::forward<Dst>(output), value))) {
            write_impl(std::forward<Dst>(output), value);
        }

        template <typename Dst>
        requires writable_to<Dst, uint64_t>
        INLINE constexpr static void write8(Dst&& output, uint64_t value) noexcept(
                noexcept(write_impl(std::forward<Dst>(output), value))) {
            write_impl(std::forward<Dst>(output), value);
        }

#if MDCOMP_HAS_INT128
        template <typename Dst>
        requires writable_to<Dst, uint128_t>
        INLINE constexpr static void write16(Dst&& output, uint128_t value) noexcept(
                noexcept(write_impl(std::forward<Dst>(output), value))) {
            write_impl(std::forward<Dst>(output), value);
        }
#endif

        template <size_t Size, typename Dst>
        requires writable_to<Dst, detail::uint_for_size_t<Size>>
        INLINE constexpr static void
                write_n(Dst&& output, detail::uint_for_size_t<Size> value) noexcept(
                        noexcept(write_impl(std::forward<Dst>(output), value))) {
            write_impl(std::forward<Dst>(output), value);
        }

        template <typename Dst, unsigned_integral From>
        requires(!std::same_as<From, bool> && writable_to<Dst, From>)
        INLINE constexpr static auto write(Dst&& output, From value) noexcept(
                noexcept(write_impl(std::forward<Dst>(output), value))) {
            static_assert(
                    !std::same_as<std::remove_cvref_t<From>, bool>,
                    "Cannot portably use bool because sizeof(bool) is "
                    "implementation-defined");
            write_impl(std::forward<Dst>(output), value);
        }

        template <typename Dst, signed_integral From>
        requires writable_to<Dst, detail::uint_for_t<From>>
        INLINE constexpr static auto write(Dst&& output, From value) noexcept(
                noexcept(write_impl(
                        std::forward<Dst>(output),
                        std::bit_cast<detail::uint_for_t<From>>(value)))) {
            write_impl(
                    std::forward<Dst>(output),
                    std::bit_cast<detail::uint_for_t<From>>(value));
        }
    };

    CONST_INLINE constexpr auto reverse(std::endian endian) noexcept {
        using enum std::endian;
        if (endian == big) {
            return little;
        }
        return big;
    }
}    // namespace utils

template <typename Src>
INLINE constexpr uint8_t read1(Src& input) noexcept(
        noexcept(utils::endian_base<std::endian::native>::read<uint8_t>(input))) {
    return utils::endian_base<std::endian::native>::read<uint8_t>(input);
}

template <typename Dst>
requires requires(Dst& output, uint8_t value) {
    utils::endian_base<std::endian::native>::write(output, value);
}
INLINE constexpr void write1(Dst& output, uint8_t const value) noexcept(
        noexcept(utils::endian_base<std::endian::native>::write(output, value))) {
    utils::endian_base<std::endian::native>::write(output, value);
}

using source_endian  = utils::endian_base<std::endian::native>;
using reverse_endian = utils::endian_base<utils::reverse(std::endian::native)>;
using big_endian     = utils::endian_base<std::endian::big>;
using little_endian  = utils::endian_base<std::endian::little>;

template <typename endian_t, utils::unsigned_integral stream_t>
requires(!std::same_as<std::remove_cv_t<stream_t>, bool>)
struct endian_input_iterator {
    using pointer          = stream_t const*;
    using reference        = stream_t const&;
    using rvalue_reference = stream_t&&;
    using difference_type  = ptrdiff_t;
    using value_type       = stream_t;

    constexpr endian_input_iterator() noexcept                             = default;
    constexpr ~endian_input_iterator() noexcept                            = default;
    constexpr endian_input_iterator(endian_input_iterator const&) noexcept = default;
    constexpr endian_input_iterator(endian_input_iterator&&) noexcept      = default;
    constexpr endian_input_iterator& operator=(endian_input_iterator const&) noexcept
            = default;
    constexpr endian_input_iterator& operator=(endian_input_iterator&&) noexcept
            = default;

    constexpr explicit endian_input_iterator(std::istream& input) noexcept(
            noexcept(++std::declval<endian_input_iterator&>()))
            : source(std::addressof(input)) {
        ++*this;
    }

    [[nodiscard]] constexpr stream_t const& operator*() const noexcept {
        return value;
    }

    [[nodiscard]] constexpr stream_t const* operator->() const noexcept {
        return std::addressof(operator*());
    }

    constexpr endian_input_iterator& operator++() noexcept(
            noexcept(endian_t::template read<stream_t>(*source))
            && noexcept(static_cast<bool>(*source))) {
        value = endian_t::template read<stream_t>(*source);
        if (!*source) {
            source = nullptr;
        }
        return *this;
    }

    constexpr endian_input_iterator operator++(int unused) noexcept(
            noexcept(++std::declval<endian_input_iterator&>())) {
        ignore_unused_variable_warning(unused);
        endian_input_iterator tmp = *this;
        ++*this;
        return tmp;
    }

    [[nodiscard]] friend constexpr bool operator==(
            endian_input_iterator const& left,
            endian_input_iterator const& right) noexcept {
        return left.source == right.source;
    }

    [[nodiscard]] friend bool operator==(
            endian_input_iterator const& iter, std::default_sentinel_t) noexcept {
        return iter.source == nullptr;
    }

    std::istream* source = nullptr;
    stream_t      value{};
};

static_assert(std::input_iterator<endian_input_iterator<big_endian, uint8_t>>);

template <typename endian_t, utils::unsigned_integral stream_t>
requires(!std::same_as<std::remove_cv_t<stream_t>, bool>)
struct endian_output_iterator {
    using reference         = endian_output_iterator&;
    using difference_type   = ptrdiff_t;
    using value_type        = stream_t;
    using iterator_category = std::output_iterator_tag;

    constexpr endian_output_iterator() noexcept                              = default;
    constexpr ~endian_output_iterator() noexcept                             = default;
    constexpr endian_output_iterator(endian_output_iterator const&) noexcept = default;
    constexpr endian_output_iterator(endian_output_iterator&&) noexcept      = default;
    constexpr endian_output_iterator& operator=(endian_output_iterator const&) noexcept
            = default;
    constexpr endian_output_iterator& operator=(endian_output_iterator&&) noexcept
            = default;

    constexpr explicit endian_output_iterator(std::ostream& output)
            : dest(std::addressof(output)) {}

    constexpr endian_output_iterator& operator*() noexcept {
        return *this;
    }

    constexpr endian_output_iterator& operator++() noexcept {
        return *this;
    }

    constexpr endian_output_iterator operator++(int unused) noexcept {
        ignore_unused_variable_warning(unused);
        return *this;
    }

    constexpr endian_output_iterator& operator=(stream_t value) noexcept(
            noexcept(endian_t::write(*dest, value))) {
        endian_t::write(*dest, value);
        return *this;
    }

    std::ostream* dest = nullptr;
};

static_assert(std::output_iterator<endian_output_iterator<big_endian, uint8_t>, uint8_t>);

#undef INLINE
#undef CONST_INLINE
#undef PURE_INLINE

#endif    // LIB_BIGENDIAN_IO_HH
