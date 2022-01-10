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

#ifndef LIB_BIGENDIAN_IO_HH
#define LIB_BIGENDIAN_IO_HH

#include <boost/mp11.hpp>
#include <boost/mpl/has_xxx.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>

#ifdef _MSC_VER
#    include <stdlib.h>
#    pragma intrinsic(_byteswap_ushort)
#    pragma intrinsic(_byteswap_ulong)
#    pragma intrinsic(_byteswap_uint64)
#endif

namespace detail {
    // Meta-programming stuff.

    template <typename Cont>
    struct has_push_back : boost::mp11::mp_same<
                                   decltype(std::declval<Cont>().push_back(
                                           typename Cont::value_type{})),
                                   void> {};

    template <typename Cont>
    struct has_size : boost::mp11::mp_same<
                              decltype(std::declval<Cont>().size()),
                              typename Cont::size_type> {};

    template <typename Cont>
    struct has_resize : boost::mp11::mp_same<
                                decltype(std::declval<Cont>().resize(
                                        typename Cont::size_type{})),
                                void> {};

    template <typename Cont>
    struct has_data : boost::mp11::mp_contains<
                              boost::mp11::mp_list<
                                      typename Cont::pointer,
                                      typename Cont::const_pointer>,
                              decltype(std::declval<Cont>().data())> {};

#ifdef __GNUG__
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
#endif
    BOOST_MPL_HAS_XXX_TRAIT_DEF(value_type)           // NOLINT
    BOOST_MPL_HAS_XXX_TRAIT_DEF(iterator)             // NOLINT
    BOOST_MPL_HAS_XXX_TRAIT_DEF(size_type)            // NOLINT
    BOOST_MPL_HAS_XXX_TRAIT_DEF(reference)            // NOLINT
    BOOST_MPL_HAS_XXX_TRAIT_DEF(iterator_category)    // NOLINT
#ifdef __GNUG__
#    pragma GCC diagnostic pop
#endif

    template <typename Cont>
    struct get_iterator_tag {
        using type = typename Cont::iterator::iterator_category;
    };

    template <typename Cont>
    using get_iterator_tag_t = typename get_iterator_tag<Cont>::type;

    template <typename Iter>
    struct get_iterator_traits {
        using type = typename std::iterator_traits<Iter>;
    };

    template <typename Iter>
    using get_iterator_traits_t = typename get_iterator_traits<Iter>::type;

    template <typename Cont, typename Tag>
    struct has_iterator_tag
            : boost::mp11::mp_same<get_iterator_tag_t<Cont>, Tag> {};

    template <typename Ptr>
    struct is_byte_pointer
            : boost::mp11::mp_and<
                      std::is_pointer<Ptr>,
                      boost::mp11::mp_contains<
                              boost::mp11::mp_list<
                                      char, unsigned char, signed char>,
                              typename std::iterator_traits<Ptr>::value_type>> {
    };

    template <typename T>
    struct is_void_pointer
            : boost::mp11::mp_and<
                      std::is_pointer<T>,
                      boost::mp11::mp_same<
                              std::remove_cv_t<typename std::pointer_traits<
                                      T>::element_type>,
                              void>> {};

    template <typename T>
    struct is_pointer_like
            : boost::mp11::mp_and<
                      std::negation<is_void_pointer<T>>,
                      has_iterator_category<get_iterator_traits_t<T>>> {};

    template <typename T>
    struct is_contiguous_container
            : boost::mp11::mp_and<
                      std::is_class<T>, has_value_type<T>, has_iterator<T>,
                      has_size_type<T>, has_reference<T>, has_push_back<T>,
                      has_size<T>, has_resize<T>, has_data<T>,
                      has_iterator_tag<T, std::random_access_iterator_tag>> {};

    template <typename T>
    struct is_contiguous_container<T&> : is_contiguous_container<T> {};

    template <typename T>
    struct tag {
        using type = T;
    };

    // base case: just fail
    template <size_t Size, typename...>
    struct select_unsigned;

    // recursive case: check using numeric_limits
    template <size_t Size, typename T, typename... Ts>
    struct select_unsigned<Size, T, Ts...>
            : std::conditional_t<
                      Size == sizeof(T), tag<T>, select_unsigned<Size, Ts...>> {
    };

    template <uint64_t Size>
    using select_unsigned_t = typename select_unsigned<
            Size, uint8_t, uint16_t, uint32_t, uint64_t>::type;

#ifdef __GNUG__
#    define ATTR_CONST __attribute__((const))
#    define CONSTEXPR  constexpr
#else
#    define ATTR_CONST
#    define CONSTEXPR
#endif
    [[nodiscard]] CONSTEXPR ATTR_CONST inline uint8_t bswap(
            uint8_t val) noexcept {
        return val;
    }

    [[nodiscard]] CONSTEXPR ATTR_CONST inline uint16_t bswap(
            uint16_t val) noexcept {
#ifdef __GNUG__
        return __builtin_bswap16(val);
#elif defined(_MSC_VER)
        return _byteswap_ushort(val);
#else
        return ((val & 0xffu) << 8) | ((val >> 8) & 0xffu);
#endif
    }

    [[nodiscard]] CONSTEXPR ATTR_CONST inline uint32_t bswap(
            uint32_t val) noexcept {
#ifdef __GNUG__
        return __builtin_bswap32(val);
#elif defined(_MSC_VER)
        return _byteswap_ulong(val);
#else
        val = ((val & 0xffffu) << 16) | ((val >> 16) & 0xffffu);
        return ((val & 0xff00ffu) << 8) | ((val >> 8) & 0xff00ffu);
#endif
    }

    [[nodiscard]] CONSTEXPR ATTR_CONST inline uint64_t bswap(
            uint64_t val) noexcept {
#ifdef __GNUG__
        return __builtin_bswap64(val);
#elif defined(_MSC_VER)
        return _byteswap_uint64(val);
#else
        val = ((val & 0xffffffffull) << 32) | ((val >> 32) & 0xffffffffull);
        val = ((val & 0xffff0000ffffull) << 16)
              | ((val >> 16) & 0xffff0000ffffull);
        return ((val & 0xff00ff00ff00ffull) << 8)
               | ((val >> 8) & 0xff00ff00ff00ffull);
#endif
    }

    struct SourceEndian {
    private:
        // Both ReadBase versions generate optimal code in GCC and clang.
        // I am splitting these cases because MSVC compiler does not understand
        // memmove (which is called my std::copy which is called by
        // std::copy_n).
        template <typename Ptr, typename T>
        static inline auto ReadBase(Ptr& in, T& val) noexcept
                -> std::enable_if_t<detail::is_byte_pointer<Ptr>::value, void> {
            std::memcpy(&val, in, sizeof(T));
        }

        template <typename Iter, typename T>
        static inline auto ReadBase(Iter& in, T& val) noexcept
                -> std::enable_if_t<!std::is_pointer<Iter>::value, void> {
            alignas(alignof(T)) std::array<char, sizeof(T)> buffer;
            std::copy_n(in, sizeof(T), std::begin(buffer));
            std::memcpy(&val, std::cbegin(buffer), sizeof(T));
        }

        template <typename Iter, typename T>
        static inline void ReadInternal(
                Iter& in, T& val, std::forward_iterator_tag) noexcept {
            ReadBase(in, val);
            std::advance(in, sizeof(T));
        }

        template <typename Iter, typename T>
        static inline void ReadInternal(
                Iter& in, T& val, std::input_iterator_tag) noexcept {
            ReadBase(in, val);
        }

        // Both WriteBase versions generate optimal code in GCC and clang.
        // I am splitting these cases because MSVC compiler does not understand
        // memmove.
        template <typename Ptr, typename T>
        static inline auto WriteBase(Ptr& out, T val) noexcept
                -> std::enable_if_t<detail::is_byte_pointer<Ptr>::value, void> {
            std::memcpy(out, &val, sizeof(T));
        }

        template <typename Iter, typename T>
        static inline auto WriteBase(Iter& out, T val) noexcept
                -> std::enable_if_t<!std::is_pointer<Iter>::value, void> {
            alignas(alignof(T)) std::array<char, sizeof(T)> buffer;
            std::memcpy(std::begin(buffer), &val, sizeof(T));
            std::copy_n(std::cbegin(buffer), sizeof(T), out);
        }

        template <typename Ptr, typename T>
        static inline void WriteInternal(
                Ptr& out, T val, std::forward_iterator_tag) noexcept {
            WriteBase(out, val);
            std::advance(out, sizeof(T));
        }

        template <typename Iter, typename T>
        static inline void WriteInternal(
                Iter& out, T val, std::output_iterator_tag) noexcept {
            WriteBase(out, val);
        }

    public:
        template <typename T>
        static inline void Read(std::istream& in, T& val) noexcept {
            alignas(alignof(T)) std::array<char, sizeof(T)> buffer;
            in.read(std::begin(buffer), sizeof(T));
            std::memcpy(&val, std::cbegin(buffer), sizeof(T));
        }

        template <typename T>
        static inline void Read(std::streambuf& in, T& val) noexcept {
            alignas(alignof(T)) std::array<char, sizeof(T)> buffer;
            in.sgetn(std::begin(buffer), sizeof(T));
            std::memcpy(&val, std::cbegin(buffer), sizeof(T));
        }

        template <typename Iter, typename T>
        static inline auto Read(Iter& in, T& val) noexcept -> std::enable_if_t<
                detail::is_pointer_like<Iter>::value, void> {
            ReadInternal(
                    in, val,
                    typename std::iterator_traits<Iter>::iterator_category());
        }

        template <typename T>
        static inline void Write(std::ostream& out, T val) noexcept {
            alignas(alignof(T)) std::array<char, sizeof(T)> buffer;
            std::memcpy(std::begin(buffer), &val, sizeof(T));
            out.write(std::cbegin(buffer), sizeof(T));
        }

        template <typename T>
        static inline void Write(std::streambuf& out, T val) noexcept {
            alignas(alignof(T)) std::array<char, sizeof(T)> buffer;
            std::memcpy(std::begin(buffer), &val, sizeof(T));
            out.sputn(std::cbegin(buffer), sizeof(T));
        }

        template <typename Cont, typename T>
        static inline auto Write(Cont& out, T val) noexcept -> std::enable_if_t<
                detail::is_contiguous_container<Cont>::value, void> {
            auto sz = out.size();
            out.resize(sz + sizeof(T));
            std::memcpy(&out[sz], &val, sizeof(T));
        }

        template <typename Iter, typename T>
        static inline auto Write(Iter& out, T val) noexcept -> std::enable_if_t<
                detail::is_pointer_like<Iter>::value, void> {
            WriteInternal(
                    out, val,
                    typename std::iterator_traits<Iter>::iterator_category());
        }
    };

    struct ReverseEndian {
        template <typename Src, typename T>
        static inline void Read(Src& in, T& val) noexcept {
            SourceEndian::Read(in, val);
            val = detail::bswap(val);
        }

        template <typename Dst, typename T>
        static inline void Write(Dst& out, T val) noexcept {
            SourceEndian::Write(out, detail::bswap(val));
        }
    };

    template <typename Base>
    struct EndianBase {
        template <typename Src>
        static inline uint8_t Read1(Src& in) noexcept {
            uint8_t val;
            Base::Read(in, val);
            return val;
        }

        template <typename Src>
        static inline uint16_t Read2(Src& in) noexcept {
            uint16_t val;
            Base::Read(in, val);
            return val;
        }

        template <typename Src>
        static inline uint32_t Read4(Src& in) noexcept {
            uint32_t val;
            Base::Read(in, val);
            return val;
        }

        template <typename Src>
        static inline uint64_t Read8(Src& in) noexcept {
            uint64_t val;
            Base::Read(in, val);
            return val;
        }

        template <size_t Size, typename Src>
        static inline auto ReadN(Src& in) noexcept
                -> detail::select_unsigned_t<Size> {
            using uint_t = detail::select_unsigned_t<Size>;
            uint_t val;
            Base::Read(in, val);
            return val;
        }

        template <typename Src, typename T>
        static inline auto Read(Src& in, T& val) noexcept
                -> std::enable_if_t<std::is_unsigned<T>::value, void> {
            Base::Read(in, val);
        }

        template <typename Src, typename T>
        static inline auto Read(Src& in, T& val) noexcept
                -> std::enable_if_t<std::is_signed<T>::value, void> {
            using uint_t = std::make_unsigned_t<T>;
            uint_t uval;
            Base::Read(in, uval);
            std::memcpy(&val, &uval, sizeof(T));
        }

        template <typename T, typename Src>
        static inline auto Read(Src& in) noexcept
                -> std::enable_if_t<std::is_unsigned<T>::value, T> {
            T val;
            Base::Read(in, val);
            return val;
        }

        template <typename T, typename Src>
        static inline auto Read(Src& in) noexcept
                -> std::enable_if_t<std::is_signed<T>::value, T> {
            using uint_t = std::make_unsigned_t<T>;
            uint_t uval;
            Base::Read(in, uval);
            T val;
            std::memcpy(&val, &uval, sizeof(T));
            return val;
        }

        template <typename Dst>
        static inline void Write1(Dst& out, uint8_t val) noexcept {
            Base::Write(out, val);
        }

        template <typename Dst>
        static inline void Write2(Dst& out, uint16_t val) noexcept {
            Base::Write(out, val);
        }

        template <typename Dst>
        static inline void Write4(Dst& out, uint32_t val) noexcept {
            Base::Write(out, val);
        }

        template <typename Dst>
        static inline void Write8(Dst& out, uint64_t val) noexcept {
            Base::Write(out, val);
        }

        template <
                typename Dst, size_t Size,
                typename Uint_t = detail::select_unsigned_t<Size>>
        static inline void WriteN(Dst& out, Uint_t val) noexcept {
            Base::Write(out, val);
        }

        template <typename Dst, typename T>
        static inline auto Write(Dst& out, T val) noexcept
                -> std::enable_if_t<std::is_unsigned<T>::value, void> {
            Base::Write(out, val);
        }
    };
}    // namespace detail

template <typename Src>
inline uint8_t Read1(Src& in) noexcept {
    uint8_t val;
    detail::SourceEndian::Read(in, val);
    return val;
}

template <typename Dst>
inline void Write1(Dst& out, uint8_t const val) noexcept {
    detail::SourceEndian::Write(out, uint8_t(val));
}

// TODO: Swap these around in big-endian architectures.
using BigEndian    = detail::EndianBase<detail::ReverseEndian>;
using LittleEndian = detail::EndianBase<detail::SourceEndian>;

#endif    // LIB_BIGENDIAN_IO_HH
