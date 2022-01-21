///
// function_ref - A low-overhead non-owning function
// Written in 2017 by Simon Brand (@TartanLlama)
//
// To the extent possible under law, the author(s) have dedicated all
// copyright and related and neighboring rights to this software to the
// public domain worldwide. This software is distributed without any warranty.
//
// You should have received a copy of the CC0 Public Domain Dedication
// along with this software. If not, see
// <http://creativecommons.org/publicdomain/zero/1.0/>.
///

#ifndef TL_FUNCTION_REF_HPP
#define TL_FUNCTION_REF_HPP

#define TL_FUNCTION_REF_VERSION_MAJOR 1
#define TL_FUNCTION_REF_VERSION_MINOR 0
#define TL_FUNCTION_REF_VERSION_PATCH 0

#include <functional>
#include <utility>

namespace tl {
    template <typename T, T>
    struct internal_function_traits;

    template <typename R, typename... Args, R (*f)(Args...)>
    struct internal_function_traits<R (*)(Args...), f> {
        static const bool is_noexcept = false;
        using function_signature      = R(Args...);
        static R prepend_void_pointer(void*, Args... args) {
            return f(std::forward<Args>(args)...);
        }
    };

    template <typename R, typename... Args, R (*f)(Args...) noexcept>
    struct internal_function_traits<R (*)(Args...) noexcept, f> {
        static const bool is_noexcept = true;
        using function_signature      = R(Args...) noexcept;
        static R prepend_void_pointer(void*, Args... args) noexcept {
            return f(std::forward<Args>(args)...);
        }
    };

    template <auto FP>
    using function_traits = internal_function_traits<decltype(FP), FP>;

    template <typename T, T>
    struct internal_member_function_traits;

    template <typename T, typename R, typename... Args, R (T::*mf)(Args...)>
    struct internal_member_function_traits<R (T::*)(Args...), mf> {
        static const bool is_noexcept = false;
        using function_signature      = R(Args...);
        // using function_pointer = R (*)(Args...);
        // using member_function_pointer = R (T::*)(Args...);
        using this_as_ref_function_signature     = R(T&, Args...);
        using this_as_pointer_function_signature = R(T*, Args...);
        using this_as_value_function_signature   = R(T, Args...);
        static R type_erase_this(void* obj, Args... args) {
            return (static_cast<T*>(obj)->*mf)(std::forward<Args>(args)...);
        }
        static R this_as_ref(void*, T& first, Args... args) {
            return (first.*mf)(std::forward<Args>(args)...);
        }
        static R this_as_pointer(void*, T* first, Args... args) {
            return (first->*mf)(std::forward<Args>(args)...);
        }
        static R this_as_value(void*, T first, Args... args) {
            return (first.*mf)(std::forward<Args>(args)...);
        }
    };

    template <typename T, typename R, typename... Args, R (T::*mf)(Args...) const>
    struct internal_member_function_traits<R (T::*)(Args...) const, mf> {
        static const bool is_noexcept = false;
        using function_signature      = R(Args...);
        // using function_pointer = R (*)(Args...);
        // using member_function_pointer = R (T::*)(Args...) const;
        using this_as_ref_function_signature     = R(const T&, Args...);
        using this_as_pointer_function_signature = R(const T*, Args...);
        using this_as_value_function_signature   = R(const T, Args...);
        static R type_erase_this(void* obj, Args... args) {
            return (static_cast<const T*>(obj)->*mf)(std::forward<Args>(args)...);
        }
        static R this_as_ref(void*, const T& first, Args... args) {
            return (first.*mf)(std::forward<Args>(args)...);
        }
        static R this_as_pointer(void*, const T* first, Args... args) {
            return (first->*mf)(std::forward<Args>(args)...);
        }
        static R this_as_value(void*, const T first, Args... args) {
            return (first.*mf)(std::forward<Args>(args)...);
        }
    };

    template <typename T, typename R, typename... Args, R (T::*mf)(Args...) noexcept>
    struct internal_member_function_traits<R (T::*)(Args...) noexcept, mf> {
        static const bool is_noexcept = true;
        using function_signature      = R(Args...) noexcept;
        // using function_pointer = R (*)(Args...) noexcept;
        // using member_function_pointer = R (T::*)(Args...) noexcept;
        using this_as_ref_function_signature     = R(T&, Args...) noexcept;
        using this_as_pointer_function_signature = R(T*, Args...) noexcept;
        using this_as_value_function_signature   = R(T, Args...) noexcept;
        static R type_erase_this(void* obj, Args... args) noexcept {
            return (static_cast<T*>(obj)->*mf)(std::forward<Args>(args)...);
        }
        static R this_as_ref(void*, T& first, Args... args) noexcept {
            return (first.*mf)(std::forward<Args>(args)...);
        }
        static R this_as_pointer(void*, T* first, Args... args) noexcept {
            return (first->*mf)(std::forward<Args>(args)...);
        }
        static R this_as_value(void*, T first, Args... args) noexcept {
            return (first.*mf)(std::forward<Args>(args)...);
        }
    };

    template <
            typename T, typename R, typename... Args, R (T::*mf)(Args...) const noexcept>
    struct internal_member_function_traits<R (T::*)(Args...) const noexcept, mf> {
        static const bool is_noexcept = true;
        using function_signature      = R(Args...) noexcept;
        // using function_pointer = R (*)(Args...) noexcept;
        // using member_function_pointer = R (T::*)(Args...) const noexcept;
        using this_as_ref_function_signature     = R(const T&, Args...) noexcept;
        using this_as_pointer_function_signature = R(const T*, Args...) noexcept;
        using this_as_value_function_signature   = R(const T, Args...) noexcept;
        static R type_erase_this(void* obj, Args... args) noexcept {
            return (static_cast<const T*>(obj)->*mf)(std::forward<Args>(args)...);
        }
        static R this_as_ref(void*, const T& first, Args... args) noexcept {
            return (first.*mf)(std::forward<Args>(args)...);
        }
        static R this_as_pointer(void*, const T* first, Args... args) noexcept {
            return (first->*mf)(std::forward<Args>(args)...);
        }
        static R this_as_value(void*, const T first, Args... args) noexcept {
            return (first.*mf)(std::forward<Args>(args)...);
        }
    };

    template <auto MFP>
    using member_function_traits = internal_member_function_traits<decltype(MFP), MFP>;

    template <typename T, T>
    struct internal_type_erase_first;

    template <typename R, typename FirstArg, typename... Args, R (*f)(FirstArg&, Args...)>
    struct internal_type_erase_first<R (*)(FirstArg& fa, Args...), f> {
        static const bool is_noexcept = false;
        using function_signature      = R(Args...);
        using function_pointer        = R (*)(Args...);
        static R type_erased_function(void* obj, Args... args) {
            return f(*static_cast<FirstArg*>(obj), std::forward<Args>(args)...);
        }
    };

    template <
            typename R, typename FirstArg, typename... Args,
            R (*f)(const FirstArg&, Args...)>
    struct internal_type_erase_first<R (*)(const FirstArg& fa, Args...), f> {
        static const bool is_noexcept = false;
        using function_signature      = R(Args...);
        using function_pointer        = R (*)(Args...);
        static R type_erased_function(void* obj, Args... args) {
            return f(*static_cast<FirstArg*>(obj), std::forward<Args>(args)...);
        }
    };

    template <typename R, typename FirstArg, typename... Args, R (*f)(FirstArg*, Args...)>
    struct internal_type_erase_first<R (*)(FirstArg* fa, Args...), f> {
        static const bool is_noexcept = false;
        using function_signature      = R(Args...);
        using function_pointer        = R (*)(Args...);
        static R type_erased_function(void* obj, Args... args) {
            return f(static_cast<FirstArg*>(obj), std::forward<Args>(args)...);
        }
    };

    template <
            typename R, typename FirstArg, typename... Args,
            R (*f)(const FirstArg*, Args...)>
    struct internal_type_erase_first<R (*)(const FirstArg* fa, Args...), f> {
        static const bool is_noexcept = false;
        using function_signature      = R(Args...);
        using function_pointer        = R (*)(Args...);
        static R type_erased_function(void* obj, Args... args) {
            return f(static_cast<FirstArg*>(obj), std::forward<Args>(args)...);
        }
    };

    template <
            typename R, typename FirstArg, typename... Args,
            R (*f)(FirstArg&, Args...) noexcept>
    struct internal_type_erase_first<R (*)(FirstArg& fa, Args...) noexcept, f> {
        static const bool is_noexcept = true;
        using function_signature      = R(Args...) noexcept;
        using function_pointer        = R (*)(Args...) noexcept;
        static R type_erased_function(void* obj, Args... args) noexcept {
            return f(*static_cast<FirstArg*>(obj), std::forward<Args>(args)...);
        }
    };

    template <
            typename R, typename FirstArg, typename... Args,
            R (*f)(const FirstArg&, Args...) noexcept>
    struct internal_type_erase_first<R (*)(const FirstArg& fa, Args...) noexcept, f> {
        static const bool is_noexcept = true;
        using function_signature      = R(Args...) noexcept;
        using function_pointer        = R (*)(Args...) noexcept;
        static R type_erased_function(void* obj, Args... args) noexcept {
            return f(*static_cast<FirstArg*>(obj), std::forward<Args>(args)...);
        }
    };

    template <
            typename R, typename FirstArg, typename... Args,
            R (*f)(FirstArg*, Args...) noexcept>
    struct internal_type_erase_first<R (*)(FirstArg* fa, Args...) noexcept, f> {
        static const bool is_noexcept = true;
        using function_signature      = R(Args...) noexcept;
        using function_pointer        = R (*)(Args...) noexcept;
        static R type_erased_function(void* obj, Args... args) noexcept {
            return f(static_cast<FirstArg*>(obj), std::forward<Args>(args)...);
        }
    };

    template <
            typename R, typename FirstArg, typename... Args,
            R (*f)(const FirstArg*, Args...) noexcept>
    struct internal_type_erase_first<R (*)(const FirstArg* fa, Args...) noexcept, f> {
        static const bool is_noexcept = true;
        using function_signature      = R(Args...) noexcept;
        using function_pointer        = R (*)(Args...) noexcept;
        static R type_erased_function(void* obj, Args... args) noexcept {
            return f(static_cast<FirstArg*>(obj), std::forward<Args>(args)...);
        }
    };

    template <auto FP>
    using type_erase_first = internal_type_erase_first<decltype(FP), FP>;

    /// A lightweight non-owning reference to a callable.
    ///
    /// Example usage:
    ///
    /// ```cpp
    /// void foo (function_ref<int(int)> func) {
    ///     std::cout << "Result is " << func(21); //42
    /// }
    ///
    /// foo([](int i) { return i*2; });
    template <class F>
    class function_ref;

    /// Specialization for function types.
    template <class R, class... Args>
    class function_ref<R(Args...)> {
    public:
        constexpr function_ref() noexcept = delete;

        /// Creates a `function_ref` which refers to the same callable as `rhs`.
        constexpr function_ref(const function_ref&) noexcept = default;

        /// Creates a `function_ref` which refers to the same callable as `rhs`.
        constexpr function_ref(function_ref&&) noexcept = default;

        /// Constructs a `function_ref` referring to `f`.
        ///
        /// \brief template <typename F> constexpr function_ref(F &&f) noexcept
        template <typename F>
            requires requires() {
                !std::is_same_v<std::decay_t<F>, function_ref>;
                std::is_invocable_r_v<R, F&&, Args...>;
            }
        // NOLINTBEGIN(google-explicit-constructor,hicpp-explicit-conversions,bugprone-forwarding-reference-overload)
        // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions,bugprone-forwarding-reference-overload)
        constexpr function_ref(F&& f) noexcept
                : obj_(reinterpret_cast<void*>(std::addressof(f))),
                  callback_([](void* obj, Args... args) -> R {
                      return std::invoke(
                              *reinterpret_cast<std::add_pointer_t<F>>(obj),
                              std::forward<Args>(args)...);
                  }) {}
        // NOLINTEND(google-explicit-constructor,hicpp-explicit-conversions,bugprone-forwarding-reference-overload)

        /// Makes `*this` refer to the same callable as `rhs`.
        constexpr function_ref& operator=(const function_ref&) noexcept = default;

        /// Makes `*this` refer to the same callable as `rhs`.
        constexpr function_ref& operator=(function_ref&&) noexcept = default;

        /// Destructor.
        constexpr ~function_ref() noexcept = default;

        /// Makes `*this` refer to `f`.
        ///
        /// \brief template <typename F> constexpr function_ref &operator=(F &&f) noexcept;
        template <typename F>
            requires std::is_invocable_r_v<R, F&&, Args...>
        constexpr function_ref& operator=(F&& f) noexcept {
            obj_      = reinterpret_cast<void*>(std::addressof(f));
            callback_ = [](void* obj, Args... args) {
                return std::invoke(
                        *reinterpret_cast<std::add_pointer_t<F>>(obj),
                        std::forward<Args>(args)...);
            };

            return *this;
        }

        /// Swaps the referred callables of `*this` and `rhs`.
        constexpr void swap(function_ref& rhs) noexcept {
            std::swap(obj_, rhs.obj_);
            std::swap(callback_, rhs.callback_);
        }

        /// Call the stored callable with the given arguments.
        R operator()(Args... args) const {
            return callback_(obj_, std::forward<Args>(args)...);
        }

        // TODO move to private section, public currently used deliberately in proposal
        // examples
        function_ref(void* obj, R (*callback)(void*, Args...) noexcept) noexcept
                : obj_{obj}, callback_{callback} {}

        static function_ref construct_from_type_erased(
                void* obj_, R (*callback_)(void*, Args...) noexcept) {
            return {obj_, callback_};
        }

    private:
        void* obj_                     = nullptr;
        R (*callback_)(void*, Args...) = nullptr;
    };

    template <class R, class... Args>
    class function_ref<R(Args...) noexcept> {
    public:
        constexpr function_ref() noexcept = delete;

        /// Creates a `function_ref` which refers to the same callable as `rhs`.
        constexpr function_ref(const function_ref&) noexcept = default;

        /// Creates a `function_ref` which refers to the same callable as `rhs`.
        constexpr function_ref(function_ref&&) noexcept = default;

        /// Constructs a `function_ref` referring to `f`.
        ///
        /// \brief template <typename F> constexpr function_ref(F &&f) noexcept
        template <typename F>
            requires requires() {
                !std::is_same_v<std::decay_t<F>, function_ref>;
                std::is_invocable_r_v<R, F&&, Args...>;
            }
        // NOLINTBEGIN(google-explicit-constructor,hicpp-explicit-conversions,bugprone-forwarding-reference-overload)
        // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions,bugprone-forwarding-reference-overload)
        constexpr function_ref(F&& f) noexcept
                : obj_(reinterpret_cast<void*>(std::addressof(f))),
                  callback_([](void* obj, Args... args) noexcept -> R {
                      return std::invoke(
                              *reinterpret_cast<std::add_pointer_t<F>>(obj),
                              std::forward<Args>(args)...);
                  }) {}
        // NOLINTEND(google-explicit-constructor,hicpp-explicit-conversions,bugprone-forwarding-reference-overload)

        /// Makes `*this` refer to the same callable as `rhs`.
        constexpr function_ref& operator=(const function_ref&) noexcept = default;

        /// Makes `*this` refer to the same callable as `rhs`.
        constexpr function_ref& operator=(function_ref&&) noexcept = default;

        /// Destructor.
        constexpr ~function_ref() noexcept = default;

        /// Makes `*this` refer to `f`.
        ///
        /// \brief template <typename F> constexpr function_ref &operator=(F &&f) noexcept;
        template <typename F>
            requires std::is_invocable_r_v<R, F&&, Args...>
        constexpr function_ref& operator=(F&& f) noexcept {
            obj_      = reinterpret_cast<void*>(std::addressof(f));
            callback_ = [](void* obj, Args... args) {
                return std::invoke(
                        *reinterpret_cast<std::add_pointer_t<F>>(obj),
                        std::forward<Args>(args)...);
            };

            return *this;
        }

        /// Swaps the referred callables of `*this` and `rhs`.
        constexpr void swap(function_ref& rhs) noexcept {
            std::swap(obj_, rhs.obj_);
            std::swap(callback_, rhs.callback_);
        }

        /// Call the stored callable with the given arguments.
        R operator()(Args... args) const noexcept {
            return callback_(obj_, std::forward<Args>(args)...);
        }

        // TODO move to private section, public currently used deliberately in proposal
        // examples
        function_ref(void* obj, R (*callback)(void*, Args...) noexcept) noexcept
                : obj_{obj}, callback_{callback} {}

        static function_ref construct_from_type_erased(
                void* obj_, R (*callback_)(void*, Args...) noexcept) {
            return {obj_, callback_};
        }

    private:
        void* obj_                              = nullptr;
        R (*callback_)(void*, Args...) noexcept = nullptr;
    };

    /// Swaps the referred callables of `lhs` and `rhs`.
    template <typename R, typename... Args>
    constexpr void swap(
            function_ref<R(Args...)>& lhs, function_ref<R(Args...)>& rhs) noexcept {
        lhs.swap(rhs);
    }

    template <typename R, typename... Args>
    constexpr void swap(
            function_ref<R(Args...) noexcept>& lhs,
            function_ref<R(Args...) noexcept>& rhs) noexcept {
        lhs.swap(rhs);
    }

    template <typename R, typename... Args>
    function_ref(R (*)(Args...)) -> function_ref<R(Args...)>;

    // TODO, will require some kind of callable traits
    // template <typename F>
    // function_ref(F) -> function_ref</* deduced if possible */>;

    // member function with type erasure
    template <auto mf, typename T>
        requires std::is_member_function_pointer_v<decltype(mf)>
    auto make_function_ref(T& obj) {
        return tl::function_ref<typename tl::internal_member_function_traits<
                decltype(mf), mf>::function_signature>::
                construct_from_type_erased(
                        std::addressof(obj), tl::internal_member_function_traits<
                                                     decltype(mf), mf>::type_erase_this);
    }

    template <auto mf, typename T>
        requires std::is_member_function_pointer_v<decltype(mf)>
    auto make_function_ref(const T& obj) {
        return tl::function_ref<typename tl::internal_member_function_traits<
                decltype(mf), mf>::function_signature>::
                construct_from_type_erased(
                        std::addressof(obj), tl::internal_member_function_traits<
                                                     decltype(mf), mf>::type_erase_this);
    }
    // member function without type erasure
    template <auto mf>
        requires std::is_member_function_pointer_v<decltype(mf)>
    auto make_function_ref() {
        return tl::function_ref<typename tl::internal_member_function_traits<
                decltype(mf), mf>::this_as_ref_function_signature>::
                construct_from_type_erased(
                        nullptr, tl::internal_member_function_traits<
                                         decltype(mf), mf>::this_as_ref);
    }

    /*
    class ref {};
    class pointer {};
    class value {};

    template <auto mf, typename T>
        requires std::is_member_function_pointer_v<decltype(mf)> && std::is_same_v<T, ref>
    auto make_function_ref() {
        return tl::function_ref<typename tl::internal_member_function_traits<
                decltype(mf), mf>::this_as_ref_function_signature>{
                nullptr,
                tl::internal_member_function_traits<decltype(mf), mf>::this_as_ref};
    }

    template <auto mf, typename T>
        requires std::is_member_function_pointer_v<decltype(mf)> && std::is_same_v<
                T, pointer>
    auto make_function_ref() {
        return tl::function_ref<typename tl::internal_member_function_traits<
                decltype(mf), mf>::this_as_pointer_function_signature>{
                nullptr,
                tl::internal_member_function_traits<decltype(mf), mf>::this_as_pointer};
    }

    template <auto mf, typename T>
        requires std::is_member_function_pointer_v<decltype(mf)> && std::is_same_v<
                T, value>
    auto make_function_ref() {
        return tl::function_ref<typename tl::internal_member_function_traits<
                decltype(mf), mf>::this_as_value_function_signature>{
                nullptr,
                tl::internal_member_function_traits<decltype(mf), mf>::this_as_value};
    }
    */
    template <typename testType>
    struct is_function_pointer {
        static const bool value
                = std::is_pointer_v<testType>
                          ? std::is_function_v<std::remove_pointer_t<testType>>
                          : false;
    };

    template <typename testType>
    constexpr inline auto is_function_pointer_v = is_function_pointer<testType>::value;

    // function with type erasure
    template <auto f, typename T>
        requires is_function_pointer_v<decltype(f)>
    auto make_function_ref(T& obj) {
        using erased_t    = tl::internal_type_erase_first<decltype(f), f>;
        using signature_t = typename erased_t::function_signature;
        return tl::function_ref<signature_t>::construct_from_type_erased(
                std::addressof(obj), erased_t::type_erased_function);
    }

    template <auto f, typename T>
        requires is_function_pointer_v<decltype(f)>
    auto make_function_ref(const T& obj) {
        using erased_t    = tl::internal_type_erase_first<decltype(f), f>;
        using signature_t = typename erased_t::function_signature;
        return tl::function_ref<signature_t>::construct_from_type_erased(
                std::addressof(obj), erased_t::type_erased_function);
    }

    // function without type erasure
    template <auto f>
        requires is_function_pointer_v<decltype(f)>
    auto make_function_ref() {
        using erased_t    = tl::internal_function_traits<decltype(f), f>;
        using signature_t = typename erased_t::function_signature;
        return tl::function_ref<signature_t>::construct_from_type_erased(
                nullptr, erased_t::prepend_void_pointer);
    }

}    // namespace tl

#endif
