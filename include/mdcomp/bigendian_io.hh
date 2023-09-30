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

#include "ignore_unused_variable_warning.hh"

#include <boost/mp11.hpp>
#include <boost/mpl/has_xxx.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

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
#    define CONST_INLINE [[using gnu: const, always_inline]] inline
// #    define PURE_INLINE  [[using gnu: pure, always_inline]] inline
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
    concept container = requires(T mut_container, T const const_container) {
        requires std::regular<T>;
        requires std::swappable<T>;
        requires std::destructible<typename T::value_type>;
        requires std::same_as<typename T::reference, typename T::value_type&>;
        requires std::same_as<typename T::const_reference, typename T::value_type const&>;
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
        { mut_container.begin() } -> std::same_as<typename T::iterator>;
        { mut_container.end() } -> std::same_as<typename T::iterator>;
        { const_container.begin() } -> std::same_as<typename T::const_iterator>;
        { const_container.end() } -> std::same_as<typename T::const_iterator>;
        { mut_container.cbegin() } -> std::same_as<typename T::const_iterator>;
        { mut_container.cend() } -> std::same_as<typename T::const_iterator>;
        { mut_container.size() } -> std::same_as<typename T::size_type>;
        { mut_container.max_size() } -> std::same_as<typename T::size_type>;
        { mut_container.empty() } -> std::same_as<bool>;
    };

    template <typename T>
    concept contiguous_container = requires() {
        requires container<T>;
        requires requires(
                T mut_container, typename T::size_type count,
                typename T::value_type value) {
            requires std::contiguous_iterator<typename T::iterator>;
            requires std::contiguous_iterator<typename T::const_iterator>;
            { mut_container.resize(count) } -> std::same_as<void>;
            { *mut_container.data() = value };
        };
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
    struct is_reverse_iterator<T const> : is_reverse_iterator<T> {};

    template <typename T>
    struct is_reverse_iterator<T&> : is_reverse_iterator<T> {};

    template <typename T>
    constexpr inline bool const is_reverse_iterator_v = is_reverse_iterator<T>::value;

    template <typename T>
    concept contiguous_reverse_iterator
            = is_reverse_iterator_v<T>
              && std::contiguous_iterator<typename T::iterator_type>;

    template <class... Ts>
    struct overloaded : public Ts... {
        using Ts::operator()...;

        constexpr explicit overloaded(Ts... callables) : Ts(callables)... {}
    };
    template <class... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;

    // Mashed together implementation based on libstdc++/libc++/MS STL.
    // GCC/clang both have a 128-bit integer type, which this implementation
    // supports; but MSVC compiler does not support a 128-bit integer, so this
    // is not portable.
    template <std::integral T>
    [[nodiscard]] CONST_INLINE constexpr T byteswap(T value) noexcept {
#if defined(__cpp_lib_byteswap) && __cpp_lib_byteswap >= 202110L
        return std::byteswap(value);
#else
        if constexpr (CHAR_BIT == 8) {
            if constexpr (sizeof(T) == 1) {
                return value;
            }
            if (!std::is_constant_evaluated()) {
                constexpr auto const builtin_bswap = overloaded(
#    ifdef __GNUG__
                        [](uint16_t const val) {
                            return __builtin_bswap16(val);
                        },
                        [](uint32_t const val) {
                            return __builtin_bswap32(val);
                        },
                        [](uint64_t const val) {
                            return __builtin_bswap64(val);
                        }
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
                );
                if constexpr (sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8) {
                    return builtin_bswap(value);
                }
#    ifdef __GNUG__
                if constexpr (sizeof(T) == 16) {
                    if constexpr (__has_builtin(__builtin_bswap128)) {
                        return __builtin_bswap128(value);
                    }
                    return (__builtin_bswap64(value >> 64U)
                            | (static_cast<T>(__builtin_bswap64(value)) << 64U));
                }
#    endif
            }
        }

        using uint_t = std::make_unsigned_t<std::remove_cv_t<T>>;
        // Fallback implementation that handles even __int24 etc.
        size_t const nbits = CHAR_BIT;

        size_t diff      = nbits * (sizeof(T) - 1);
        uint_t mask1     = std::numeric_limits<uint8_t>::max();
        auto   mask2     = static_cast<uint_t>(mask1 << diff);
        uint_t new_value = value;
        for (size_t ii = 0; ii < sizeof(T) / 2; ++ii) {
            uint_t byte1 = new_value & mask1;
            uint_t byte2 = new_value & mask2;
            new_value    = static_cast<uint_t>(
                    new_value ^ byte1 ^ byte2 ^ (byte1 << diff) ^ (byte2 >> diff));
            mask1 = static_cast<uint_t>(mask1 << nbits);
            mask2 = static_cast<uint_t>(mask2 >> nbits);
            diff -= 2ULL * nbits;
        }
        return uint_t(new_value & std::numeric_limits<uint_t>::max());
#endif
    }

    template <std::endian endian>
    struct endian_base {
    private:
        template <std::unsigned_integral To, typename Stream>
        requires requires(Stream stream, char* pointer, std::streamsize count) {
            { stream.read(pointer, count) } -> std::common_reference_with<Stream>;
        }
        [[nodiscard]] INLINE constexpr static To read_impl(Stream&& input) noexcept(
                noexcept(input.read(std::declval<char*>(), sizeof(To)))) {
            alignas(alignof(To)) std::array<char, sizeof(To)> buffer;
            std::forward<Stream>(input).read(buffer.data(), sizeof(To));
            if constexpr (endian != std::endian::native) {
                return detail::byteswap(std::bit_cast<To>(buffer));
            } else {
                return std::bit_cast<To>(buffer);
            }
        }

        template <std::unsigned_integral To, typename Stream>
        requires requires(Stream stream, char* pointer, std::streamsize count) {
            { stream.sgetn(pointer, count) } -> std::same_as<std::streamsize>;
        }
        [[nodiscard]] INLINE constexpr static To read_impl(Stream&& input) noexcept(
                noexcept(input.sgetn(std::declval<char*>(), sizeof(To)))) {
            alignas(alignof(To)) std::array<char, sizeof(To)> buffer;
            std::forward<Stream>(input).sgetn(buffer.data(), sizeof(To));
            if constexpr (endian != std::endian::native) {
                return detail::byteswap(std::bit_cast<To>(buffer));
            } else {
                return std::bit_cast<To>(buffer);
            }
        }

        template <std::unsigned_integral To, typename IterRef>
        requires(byte_input_iterator<std::remove_cvref_t<IterRef>>)
        // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
        [[nodiscard]] INLINE constexpr static To read_impl(IterRef&& input) noexcept {
            using iterator = std::remove_cvref_t<IterRef>;
            if constexpr (contiguous_reverse_iterator<iterator>) {
                std::advance(input, sizeof(To));
            }
            alignas(alignof(To)) std::array<char, sizeof(To)> buffer{};
            // Both of these versions generate optimal code in GCC and
            // clang. I am splitting these cases because MSVC compiler does
            // cannot see through the iterator abstraction.
            if constexpr (
                    (std::contiguous_iterator<iterator>)
                    || (contiguous_reverse_iterator<iterator>)) {
                std::ranges::copy_n(std::to_address(input), sizeof(To), buffer.data());
            } else {
                std::ranges::copy_n(input, sizeof(To), std::ranges::begin(buffer));
            }
            To const value = [&]() {
                if constexpr (contiguous_reverse_iterator<iterator>) {
                    if constexpr (endian == std::endian::native) {
                        return detail::byteswap(std::bit_cast<To>(buffer));
                    } else {
                        return std::bit_cast<To>(buffer);
                    }
                } else {
                    if constexpr (endian != std::endian::native) {
                        return detail::byteswap(std::bit_cast<To>(buffer));
                    } else {
                        return std::bit_cast<To>(buffer);
                    }
                }
            }();
            if constexpr ((std::forward_iterator<iterator>)&&(
                                  !contiguous_reverse_iterator<iterator>)) {
                std::advance(input, sizeof(To));
            }
            return value;
        }

        template <std::unsigned_integral From, typename Stream>
        requires requires(Stream stream, char const* pointer, std::streamsize count) {
            { stream.write(pointer, count) } -> std::common_reference_with<Stream>;
        }
        INLINE constexpr static void write_impl(Stream&& output, From value) noexcept(
                noexcept(output.write(std::declval<char const*>(), sizeof(From)))) {
            if constexpr (endian != std::endian::native) {
                value = detail::byteswap(value);
            }
            alignas(alignof(From)) std::array<char, sizeof(From)> buffer
                    = std::bit_cast<decltype(buffer)>(value);
            std::forward<Stream>(output).write(buffer.data(), sizeof(From));
        }

        template <std::unsigned_integral From, typename Stream>
        requires requires(Stream stream, char const* pointer, std::streamsize count) {
            { stream.sputn(pointer, count) } -> std::same_as<std::streamsize>;
        }
        INLINE constexpr static void write_impl(Stream&& output, From value) noexcept(
                noexcept(output.sputn(std::declval<char const*>(), sizeof(From)))) {
            if constexpr (endian != std::endian::native) {
                value = detail::byteswap(value);
            }
            alignas(alignof(From)) std::array<char, sizeof(From)> buffer
                    = std::bit_cast<decltype(buffer)>(value);
            std::forward<Stream>(output).sputn(buffer.data(), sizeof(From));
        }

        template <contiguous_container Cont, std::unsigned_integral From>
        INLINE constexpr static void write_impl(Cont& output, From value) noexcept(
                noexcept(output.resize(std::declval<size_t>()))) {
            if constexpr (endian != std::endian::native) {
                value = detail::byteswap(value);
            }
            auto const size = output.size();
            output.resize(size + sizeof(From));
            write_impl(std::addressof(output[size]), value);
        }

        template <typename IterRef, std::unsigned_integral From>
        requires(byte_output_iterator<std::remove_cvref_t<IterRef>>)
        // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
        INLINE constexpr static void write_impl(IterRef&& output, From value) noexcept {
            // Both of these versions generate optimal code in GCC and
            // clang. I am splitting these cases because MSVC compiler does
            // cannot see through the iterator abstraction.
            using iterator = std::remove_cvref_t<IterRef>;
            if constexpr (contiguous_reverse_iterator<iterator>) {
                std::advance(output, sizeof(From));
                if constexpr (endian == std::endian::native) {
                    value = detail::byteswap(value);
                }
            } else {
                if constexpr (endian != std::endian::native) {
                    value = detail::byteswap(value);
                }
            }
            alignas(alignof(From)) std::array<char, sizeof(From)> buffer
                    = std::bit_cast<decltype(buffer)>(value);
            if constexpr (
                    (std::contiguous_iterator<iterator>)
                    || (contiguous_reverse_iterator<iterator>)) {
                std::ranges::copy(buffer, std::to_address(output));
            } else {
                std::ranges::copy(buffer, output);
            }
            if constexpr ((std::forward_iterator<iterator>)&&(
                                  !contiguous_reverse_iterator<iterator>)) {
                std::advance(output, sizeof(From));
            }
        }

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

        template <size_t Size, typename Src>
        [[nodiscard]] INLINE constexpr static auto read_n(Src&& input) noexcept(noexcept(
                read_impl<detail::select_unsigned_t<Size>>(std::forward<Src>(input)))) {
            return read_impl<detail::select_unsigned_t<Size>>(std::forward<Src>(input));
        }

        template <typename Src, std::integral To>
        INLINE constexpr static auto read(Src&& input, To& value) noexcept(
                noexcept(read_impl<To>(std::forward<Src>(input)))) {
            value = read_impl<To>(std::forward<Src>(input));
        }

        template <std::unsigned_integral To, typename Src>
        [[nodiscard]] INLINE constexpr static auto read(Src&& input) noexcept(
                noexcept(read_impl<To>(std::forward<Src>(input)))) {
            static_assert(
                    !std::same_as<std::remove_cvref_t<To>, bool>,
                    "Cannot portably use bool because sizeof(bool) is "
                    "implementation-defined");
            return read_impl<To>(std::forward<Src>(input));
        }

        template <std::signed_integral To, typename Src>
        [[nodiscard]] INLINE constexpr static auto read(Src&& input) noexcept(
                noexcept(std::bit_cast<To>(
                        read_impl<std::make_unsigned_t<To>>(std::forward<Src>(input))))) {
            return std::bit_cast<To>(
                    read_impl<std::make_unsigned_t<To>>(std::forward<Src>(input)));
        }

        template <typename Dst>
        INLINE constexpr static void write1(Dst&& output, uint8_t value) noexcept(
                noexcept(write_impl(std::forward<Dst>(output), value))) {
            write_impl(std::forward<Dst>(output), value);
        }

        template <typename Dst>
        INLINE constexpr static void write2(Dst&& output, uint16_t value) noexcept(
                noexcept(write_impl(std::forward<Dst>(output), value))) {
            write_impl(std::forward<Dst>(output), value);
        }

        template <typename Dst>
        INLINE constexpr static void write4(Dst&& output, uint32_t value) noexcept(
                noexcept(write_impl(std::forward<Dst>(output), value))) {
            write_impl(std::forward<Dst>(output), value);
        }

        template <typename Dst>
        INLINE constexpr static void write8(Dst&& output, uint64_t value) noexcept(
                noexcept(write_impl(std::forward<Dst>(output), value))) {
            write_impl(std::forward<Dst>(output), value);
        }

        template <
                size_t Size, typename Dst,
                typename Uint_t = detail::select_unsigned_t<Size>>
        INLINE constexpr static void write_n(Dst&& output, Uint_t value) noexcept(
                noexcept(write_impl(std::forward<Dst>(output), value))) {
            write_impl(std::forward<Dst>(output), value);
        }

        template <typename Dst, std::unsigned_integral From>
        INLINE constexpr static auto write(Dst&& output, From value) noexcept(
                noexcept(write_impl(std::forward<Dst>(output), value))) {
            static_assert(
                    !std::same_as<std::remove_cvref_t<From>, bool>,
                    "Cannot portably use bool because sizeof(bool) is "
                    "implementation-defined");
            write_impl(std::forward<Dst>(output), value);
        }

        template <typename Dst, std::signed_integral From>
        INLINE constexpr static auto write(Dst&& output, From value) noexcept(
                noexcept(write_impl(
                        std::forward<Dst>(output),
                        std::bit_cast<std::make_unsigned_t<From>>(value)))) {
            write_impl(
                    std::forward<Dst>(output),
                    std::bit_cast<std::make_unsigned_t<From>>(value));
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
INLINE constexpr uint8_t read1(Src& input) noexcept(
        noexcept(detail::endian_base<std::endian::native>::read<uint8_t>(input))) {
    return detail::endian_base<std::endian::native>::read<uint8_t>(input);
}

template <typename Dst>
INLINE constexpr void write1(Dst& output, uint8_t const value) noexcept(
        noexcept(detail::endian_base<std::endian::native>::write(output, value))) {
    detail::endian_base<std::endian::native>::write(output, value);
}

using source_endian  = detail::endian_base<std::endian::native>;
using reverse_endian = detail::endian_base<detail::reverse(std::endian::native)>;
using big_endian     = detail::endian_base<std::endian::big>;
using little_endian  = detail::endian_base<std::endian::little>;

template <typename endian_t, std::unsigned_integral stream_t>
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

    constexpr explicit endian_input_iterator(std::istream& input)
            : source(std::addressof(input)) {}

    [[nodiscard]] constexpr stream_t const& operator*() const noexcept {
        return value;
    }

    [[nodiscard]] constexpr stream_t const* operator->() const noexcept {
        return std::addressof(operator*());
    }

    constexpr endian_input_iterator& operator++() noexcept {
        value = endian_t::template read<stream_t>(*source);
        return *this;
    }

    constexpr endian_input_iterator operator++(int unused) noexcept {
        ignore_unused_variable_warning(unused);
        endian_input_iterator tmp = *this;
        value                     = endian_t::template read<stream_t>(*source);
        return tmp;
    }

    [[nodiscard]] friend constexpr bool operator==(
            endian_input_iterator const& left,
            endian_input_iterator const& right) noexcept {
        return left.source == right.source;
    }

    [[nodiscard]] friend bool operator==(
            endian_input_iterator const& iter, std::default_sentinel_t) noexcept {
        return iter.source != nullptr;
    }

    std::istream* source = nullptr;
    stream_t      value{};
};

static_assert(std::input_iterator<endian_input_iterator<big_endian, uint8_t>>);

template <typename endian_t, std::unsigned_integral stream_t>
struct endian_output_iterator {
    using reference       = endian_output_iterator&;
    using difference_type = ptrdiff_t;

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

    constexpr endian_output_iterator& operator++(int unused) noexcept {
        ignore_unused_variable_warning(unused);
        return *this;
    }

    constexpr endian_output_iterator& operator=(stream_t value) noexcept {
        endian_t::write(*dest, value);
        return *this;
    }

    std::ostream* dest = nullptr;
};

static_assert(std::output_iterator<endian_output_iterator<big_endian, uint8_t>, uint8_t>);

#undef INLINE
#undef CONST_INLINE
// #undef PURE_INLINE

#endif    // LIB_BIGENDIAN_IO_HH
