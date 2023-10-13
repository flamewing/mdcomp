/*
 * Copyright (C) Flamewing 2022 <flamewing.sonic@gmail.com>
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

#ifndef LIB_STREAM_UTILS_HH
#define LIB_STREAM_UTILS_HH

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <iterator>
#include <ostream>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>

namespace detail {
    template <typename T1, typename T2>
    requires requires(T1 value1, T2 value2) {
        value1 + value2;
        value1 - value2;
        value1* value2;
        value1 / value2;
        value1 % value2;
    }
    constexpr inline auto round_up(T1 const value, T2 const factor) noexcept {
        constexpr decltype(factor) const one{1};
        return ((value + factor - one) / factor) * factor;
    };

    inline void pad_to_even(std::ostream& dest) {
        if ((dest.tellp() % 2) != 0) {
            dest.put(0);
        }
    }

    inline void pad_to_multiple(std::ostream& dest, std::streamoff multiple) {
        // Padding between modules
        int64_t const padding_end = round_up(dest.tellp(), multiple);
        while (dest.tellp() < padding_end) {
            dest.put(0);
        }
    }

    // All of the following is stolen from MS-STL.
    template <typename>
    // false value attached to a dependent name (for static_assert)
    constexpr inline bool always_false = false;

    struct from_range_t {
        explicit from_range_t() = default;
    };

    constexpr inline from_range_t from_range;

    template <typename Ty>
    using with_reference = Ty&;

    template <typename Ty>
    concept can_reference = requires { typename with_reference<Ty>; };

    template <typename It>
    concept cpp17_iterator = requires(It iter) {
        { *iter } -> can_reference;
        { ++iter } -> std::same_as<It&>;
        { *iter++ } -> can_reference;
    } && std::copyable<It>;

    template <typename Ty>
    concept has_member_difference_type = requires { typename Ty::difference_type; };

    template <typename Ty>
    concept has_member_value_type = requires { typename Ty::value_type; };

    namespace pipe {
        template <typename derived>
        struct base {};

        template <typename Ty>
        Ty* derived_from_range_adaptor_closure(base<Ty>&);    // not defined

        template <typename Ty>
        concept range_adaptor_closure_object
                = !std::ranges::range<std::remove_cvref_t<Ty>>
                  && requires(std::remove_cvref_t<Ty>& inst) {
                         {
                             pipe::derived_from_range_adaptor_closure(inst)
                         } -> std::same_as<std::remove_cvref_t<Ty>*>;
                     };

        template <typename ClosureLeft, typename ClosureRight>
        struct pipeline : base<pipeline<ClosureLeft, ClosureRight>> {
            static_assert(range_adaptor_closure_object<ClosureLeft>);
            static_assert(range_adaptor_closure_object<ClosureRight>);

            [[no_unique_address]] ClosureLeft  left;
            [[no_unique_address]] ClosureRight right;

            template <typename Ty1, typename Ty2>
            constexpr explicit pipeline(Ty1&& val1, Ty2&& val2) noexcept(
                    std::is_nothrow_constructible_v<Ty1, ClosureLeft>
                    && std::is_nothrow_constructible_v<Ty2, ClosureRight>)
                    : left(std::forward<Ty1>(val1)), right(std::forward<Ty2>(val2)) {}

            template <typename Ty>
            [[nodiscard]] constexpr auto operator()(Ty&& val) & noexcept(
                    noexcept(right(left(std::forward<Ty>(val)))))
            requires requires { right(left(std::forward<Ty>(val))); }
            {
                return right(left(std::forward<Ty>(val)));
            }

            template <typename Ty>
            [[nodiscard]] constexpr auto operator()(Ty&& val) const& noexcept(
                    noexcept(right(left(std::forward<Ty>(val)))))
            requires requires { right(left(std::forward<Ty>(val))); }
            {
                return right(left(std::forward<Ty>(val)));
            }

            template <typename Ty>
            [[nodiscard]] constexpr auto operator()(Ty&& val) && noexcept(
                    noexcept(std::move(right)(std::move(left)(std::forward<Ty>(val)))))
            requires requires {
                std::move(right)(std::move(left)(std::forward<Ty>(val)));
            }
            {
                return std::move(right)(std::move(left)(std::forward<Ty>(val)));
            }

            template <typename Ty>
            [[nodiscard]] constexpr auto operator()(Ty&& val) const&& noexcept(
                    noexcept(std::move(right)(std::move(left)(std::forward<Ty>(val)))))
            requires requires {
                std::move(right)(std::move(left)(std::forward<Ty>(val)));
            }
            {
                return std::move(right)(std::move(left)(std::forward<Ty>(val)));
            }
        };

        template <typename Ty1, typename Ty2>
        pipeline(Ty1, Ty2) -> pipeline<Ty1, Ty2>;

        template <typename Left, typename Right>
        requires range_adaptor_closure_object<Left> && range_adaptor_closure_object<Right>
                 && std::constructible_from<std::remove_cvref_t<Left>, Left>
                 && std::constructible_from<std::remove_cvref_t<Right>, Right>
        [[nodiscard]] constexpr auto operator|(Left&& left, Right&& right) noexcept(
                noexcept(pipeline{
                        std::forward<Left>(left), std::forward<Right>(right)})) {
            return pipeline{std::forward<Left>(left), std::forward<Right>(right)};
        }

        template <typename Left, typename Right>
        requires(range_adaptor_closure_object<Right> && std::ranges::range<Left>)
        [[nodiscard]] constexpr auto operator|(Left&& left, Right&& right) noexcept(
                noexcept(std::forward<Right>(right)(std::forward<Left>(left))))
        requires requires { static_cast<Right&&>(right)(static_cast<Left&&>(left)); }
        {
            return std::forward<Right>(right)(std::forward<Left>(left));
        }
    }    // namespace pipe

    template <typename derived>
    requires std::is_class_v<derived> && std::same_as<derived, std::remove_cv_t<derived>>
    class range_adaptor_closure : public pipe::base<derived> {};

    template <typename Fn, typename... Types>
    class range_closure : public pipe::base<range_closure<Fn, Types...>> {
    private:
        template <typename SelfTy, typename Ty, size_t... Idx>
        constexpr static decltype(auto)
                call(SelfTy&& self, Ty&& arg, std::index_sequence<Idx...>) noexcept(
                        noexcept(Fn{}(
                                std::forward<Ty>(arg),
                                std::get<Idx>(std::forward<SelfTy>(self).captures)...))) {
            static_assert(std::same_as<std::index_sequence<Idx...>, indices>);
            return Fn{}(
                    std::forward<Ty>(arg),
                    std::get<Idx>(std::forward<SelfTy>(self).captures)...);
        }

    public:
        // We assume that Fn is the type of a customization point object. That means
        // 1. The behavior of operator() is independent of cvref qualifiers, so we can use
        // `invocable<Fn, ` without
        //    loss of generality, and
        // 2. Fn must be default-constructible and stateless, so we can create instances
        // "on-the-fly" and avoid
        //    storing a copy.

        static_assert((std::same_as<std::decay_t<Types>, Types> && ...));
        static_assert(std::is_empty_v<Fn> && std::is_default_constructible_v<Fn>);

        template <typename... UTypes>
        requires(std::same_as<std::decay_t<UTypes>, Types> && ...)
        constexpr explicit range_closure(UTypes&&... args) noexcept(
                std::conjunction_v<std::is_nothrow_constructible<Types, UTypes>...>)
                : captures(std::forward<UTypes>(args)...) {}

        void operator()(auto&&) &       = delete;
        void operator()(auto&&) const&  = delete;
        void operator()(auto&&) &&      = delete;
        void operator()(auto&&) const&& = delete;

        using indices = std::index_sequence_for<Types...>;

        template <typename Ty>
        requires std::invocable<Fn, Ty, Types&...>
        constexpr decltype(auto) operator()(Ty && arg) & noexcept(
                noexcept(call(*this, std::forward<Ty>(arg), indices{}))) {
            return call(*this, std::forward<Ty>(arg), indices{});
        }

        template <typename Ty>
        requires std::invocable<Fn, Ty, Types const&...>
        constexpr decltype(auto) operator()(Ty && arg) const& noexcept(
                noexcept(call(*this, std::forward<Ty>(arg), indices{}))) {
            return call(*this, std::forward<Ty>(arg), indices{});
        }

        template <typename Ty>
        requires std::invocable<Fn, Ty, Types...>
        constexpr decltype(auto) operator()(Ty && arg) && noexcept(
                noexcept(call(std::move(*this), std::forward<Ty>(arg), indices{}))) {
            return call(std::move(*this), std::forward<Ty>(arg), indices{});
        }

        template <typename Ty>
        requires std::invocable<Fn, Ty, Types const...>
        constexpr decltype(auto) operator()(Ty && arg) const&& noexcept(
                noexcept(call(std::move(*this), std::forward<Ty>(arg), indices{}))) {
            return call(std::move(*this), std::forward<Ty>(arg), indices{});
        }

    private:
        std::tuple<Types...> captures;
    };

    template <typename It>
    concept cpp17_input_iterator
            = cpp17_iterator<It> && std::equality_comparable<It>
              && has_member_difference_type<std::incrementable_traits<It>>
              && has_member_value_type<std::indirectly_readable_traits<It>>
              && requires(It iter) {
                     typename std::common_reference_t<
                             std::iter_reference_t<It>&&,
                             typename std::indirectly_readable_traits<It>::value_type&>;
                     typename std::common_reference_t<
                             decltype(*iter++)&&,
                             typename std::indirectly_readable_traits<It>::value_type&>;
                     requires std::signed_integral<
                             typename std::incrementable_traits<It>::difference_type>;
                 };

    template <typename range_t, typename container_t>
    concept sized_and_reservable
            = std::ranges::sized_range<range_t> && std::ranges::sized_range<container_t>
              && requires(
                      container_t&                                 cont,
                      std::ranges::range_size_t<container_t> const count) {
                     cont.reserve(count);
                     {
                         cont.capacity()
                     } -> std::same_as<std::ranges::range_size_t<container_t>>;
                     {
                         cont.max_size()
                     } -> std::same_as<std::ranges::range_size_t<container_t>>;
                 };

    template <typename range_t, typename container_t>
    concept ref_converts = std::convertible_to<
            std::ranges::range_reference_t<range_t>,
            std::ranges::range_value_t<container_t>>;

    template <typename range_t, typename container_t, typename... types_t>
    concept converts_direct_constructible
            = ref_converts<range_t, container_t>    //
              && std::constructible_from<container_t, range_t, types_t...>;

    template <typename range_t, typename container_t, typename... types_t>
    concept converts_tag_constructible
            = ref_converts<range_t, container_t>
              // per LWG issue unnumbered as of 2022-08-08
              && std::constructible_from<
                      container_t, from_range_t const&, range_t, types_t...>;

    template <typename range_t, typename container_t, typename... types_t>
    concept converts_and_common_constructible
            = ref_converts<range_t, container_t>
              && std::ranges::common_range<range_t>                        //
              && cpp17_input_iterator<std::ranges::iterator_t<range_t>>    //
              && std::constructible_from<
                      container_t, std::ranges::iterator_t<range_t>,
                      std::ranges::iterator_t<range_t>, types_t...>;

    template <typename container_t, typename reference>
    concept can_push_back
            = requires(container_t& cont) { cont.push_back(std::declval<reference>()); };

    template <typename container_t, typename reference>
    concept can_insert_end = requires(container_t& cont) {
        cont.insert(cont.end(), std::declval<reference>());
    };

    template <typename range_t, typename container_t, typename... types_t>
    concept converts_constructible_insertable
            = ref_converts<range_t, container_t>
              && std::constructible_from<container_t, types_t...>
              && (can_push_back<container_t, std::ranges::range_reference_t<range_t>>
                  || can_insert_end<
                          container_t, std::ranges::range_reference_t<range_t>>);

    template <typename reference, typename container_t>
    [[nodiscard]] constexpr auto container_inserter(container_t& cont) {
        if constexpr (can_push_back<container_t, reference>) {
            return std::back_insert_iterator{cont};
        } else {
            return std::insert_iterator{cont, cont.end()};
        }
    }

    template <typename container_t, std::ranges::input_range range_t, typename... types_t>
    requires(!std::ranges::view<container_t>)
    [[nodiscard]] constexpr container_t to(range_t&& range, types_t&&... args) {
        if constexpr (converts_direct_constructible<range_t, container_t, types_t...>) {
            return container_t(
                    std::forward<range_t>(range), std::forward<types_t>(args)...);
        } else if constexpr (converts_tag_constructible<
                                     range_t, container_t, types_t...>) {
            return container_t(
                    from_range, std::forward<range_t>(range),
                    std::forward<types_t>(args)...);
        } else if constexpr (converts_and_common_constructible<
                                     range_t, container_t, types_t...>) {
            return container_t(
                    std::ranges::begin(range), std::ranges::end(range),
                    std::forward<types_t...>(args)...);
        } else if constexpr (converts_constructible_insertable<
                                     range_t, container_t, types_t...>) {
            container_t cont(std::forward<types_t>(args)...);
            if constexpr (sized_and_reservable<range_t, container_t>) {
                cont.reserve(std::ranges::size(range));
            }
            std::ranges::copy(
                    range,
                    container_inserter<std::ranges::range_reference_t<range_t>>(cont));
            return cont;
        } else if constexpr (std::ranges::input_range<
                                     std::ranges::range_reference_t<range_t>>) {
            auto const operation = [](auto&& elem) {
                return to<std::ranges::range_value_t<container_t>>(
                        std::forward<decltype(elem)>(elem));
            };
            return to<container_t>(
                    std::ranges::transform(range, operation),
                    std::forward<types_t>(args)...);
        } else {
            static_assert(
                    always_false<container_t>,
                    "the program is ill-formed per N4910 [range.utility.conv.to]/1.3");
        }
    }

    template <typename container_t>
    struct to_class_fn {
        static_assert(!std::ranges::view<container_t>);

        template <std::ranges::input_range range_t, typename... types_t>
        [[nodiscard]] constexpr auto operator()(range_t&& range, types_t&&... args) const
        requires requires {
            to<container_t>(std::forward<range_t>(range), std::forward<types_t>(args)...);
        }
        {
            return to<container_t>(
                    std::forward<range_t>(range), std::forward<types_t>(args)...);
        }
    };

    template <typename container_t, typename... types_t>
    requires(!std::ranges::view<container_t>)
    [[nodiscard]] constexpr auto to(types_t&&... args) {
        return range_closure<to_class_fn<container_t>, std::decay_t<types_t>...>{
                std::forward<types_t>(args)...};
    }

    template <std::ranges::input_range range_t>
    struct phony_input_iterator {
        using iterator_category = std::input_iterator_tag;
        using value_type        = std::ranges::range_value_t<range_t>;
        using difference_type   = ptrdiff_t;
        using pointer   = std::add_pointer_t<std::ranges::range_reference_t<range_t>>;
        using reference = std::ranges::range_reference_t<range_t>;

        reference operator*() const;
        pointer   operator->() const;

        phony_input_iterator& operator++();
        phony_input_iterator  operator++(int);

        bool operator==(phony_input_iterator const&) const;
    };

    template <template <typename...> typename count, typename range_t, typename... args_t>
    auto to_helper() {
        if constexpr (requires {
                          count(std::declval<range_t>(), std::declval<args_t>()...);
                      }) {
            return static_cast<decltype(count(
                    std::declval<range_t>(), std::declval<args_t>()...))*>(nullptr);
        } else if constexpr (requires {
                                 count(from_range, std::declval<range_t>(),
                                       std::declval<args_t>()...);
                             }) {
            return static_cast<decltype(count(
                    from_range, std::declval<range_t>(), std::declval<args_t>()...))*>(
                    nullptr);
        } else if constexpr (requires {
                                 count(std::declval<phony_input_iterator<range_t>>(),
                                       std::declval<phony_input_iterator<range_t>>(),
                                       std::declval<args_t>()...);
                             }) {
            return static_cast<decltype(count(
                    std::declval<phony_input_iterator<range_t>>(),
                    std::declval<phony_input_iterator<range_t>>(),
                    std::declval<args_t>()...))*>(nullptr);
        }
    }

    template <
            template <typename...> typename container_t, std::ranges::input_range range_t,
            typename... types_t,
            typename deduced = std::remove_pointer_t<
                    decltype(to_helper<container_t, range_t, types_t...>())>>
    [[nodiscard]] constexpr deduced to(range_t&& range, types_t&&... args) {
        return to<deduced>(std::forward<range_t>(range), std::forward<types_t>(args)...);
    }

    template <template <typename...> typename container_t>
    struct to_template_fn {
        template <
                std::ranges::input_range range_t, typename... types_t,
                typename deduced = std::remove_pointer_t<
                        decltype(to_helper<container_t, range_t, types_t...>())>>
        [[nodiscard]] constexpr auto operator()(
                range_t&& range, types_t&&... args) const {
            return to<deduced>(
                    std::forward<range_t>(range), std::forward<types_t>(args)...);
        }
    };

    template <template <typename...> typename container_t, typename... types_t>
    [[nodiscard]] constexpr auto to(types_t&&... args) {
        return range_closure<to_template_fn<container_t>, std::decay_t<types_t>...>{
                std::forward<types_t>(args)...};
    }
}    // namespace detail

#endif    // LIB_STREAM_UTILS_HH
