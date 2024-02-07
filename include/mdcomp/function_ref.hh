#pragma once

#include <functional>
#include <type_traits>
#include <utility>

namespace std23 {

    template <auto V>
    struct nontype_t {
        explicit nontype_t() = default;
    };

    template <auto V>
    constexpr inline nontype_t<V> nontype{};

    template <class R, class F, class... Args>
    requires std::is_invocable_r_v<R, F, Args...>
    constexpr R invoke_r(F&& callable, Args&&... args) noexcept(
            std::is_nothrow_invocable_r_v<R, F, Args...>) {
        if constexpr (std::is_void_v<R>) {
            std::invoke(std::forward<F>(callable), std::forward<Args>(args)...);
        } else {
            return std::invoke(std::forward<F>(callable), std::forward<Args>(args)...);
        }
    }

    template <class Sig>
    struct _qual_fn_sig;

    template <class R, class... Args>
    struct _qual_fn_sig<R(Args...)> {
        using function = R(Args...);
        template <class T>
        using cv = T;

        constexpr static bool is_noexcept = false;

        template <class... T>
        constexpr static bool is_invocable_using
                = std::is_invocable_r_v<R, T..., Args...>;
    };

    template <class R, class... Args>
    struct _qual_fn_sig<R(Args...) noexcept> {
        using function = R(Args...);
        template <class T>
        using cv = T;

        constexpr static bool is_noexcept = true;

        template <class... T>
        constexpr static bool is_invocable_using
                = std::is_nothrow_invocable_r_v<R, T..., Args...>;
    };

    template <class R, class... Args>
    struct _qual_fn_sig<R(Args...) const> : _qual_fn_sig<R(Args...)> {
        template <class T>
        using cv = T const;
    };

    template <class R, class... Args>
    struct _qual_fn_sig<R(Args...) const noexcept> : _qual_fn_sig<R(Args...) noexcept> {
        template <class T>
        using cv = T const;
    };

    // See also: https://www.agner.org/optimize/calling_conventions.pdf
    template <class T>
    constexpr inline auto select_param_type = [] {
        if constexpr (std::is_trivially_copyable_v<T>) {
            return std::type_identity<T>();
        } else {
            return std::add_rvalue_reference<T>();
        }
    };

    template <class T>
    using param_t = typename std::invoke_result_t<decltype(select_param_type<T>)>::type;

    template <class T, class Self>
    constexpr inline bool is_not_self = not std::is_same_v<std::remove_cvref_t<T>, Self>;

    template <class T>
    struct unwrap_reference {
        using type = T;
    };

    template <class U>
    struct unwrap_reference<std::reference_wrapper<U>> {
        using type = U;
    };

    template <class U>
    struct unwrap_reference<std::reference_wrapper<U> const> {
        using type = U;
    };

    template <class U>
    struct unwrap_reference<std::reference_wrapper<U> volatile> {
        using type = U;
    };

    template <class U>
    struct unwrap_reference<std::reference_wrapper<U> const volatile> {
        using type = U;
    };

    template <class T>
    using remove_and_unwrap_reference_t =
            typename unwrap_reference<std::remove_reference_t<T>>::type;

    struct function_ref_base {
        union storage {
            void*       pointer = nullptr;
            void const* const_pointer;
            void (*fun_pointer)();

            constexpr storage() noexcept = default;

            template <class T>
            requires std::is_object_v<T>
            constexpr explicit storage(T* pointer_in) noexcept : pointer(pointer_in) {}

            template <class T>
            requires std::is_object_v<T>
            constexpr explicit storage(T const* pointer_in) noexcept
                    : const_pointer(pointer_in) {}

            template <class T>
            requires std::is_function_v<T>
            constexpr explicit storage(T* pointer_in) noexcept
                    : fun_pointer(reinterpret_cast<decltype(fun_pointer)>(pointer_in)) {}
        };

        template <class T>
        constexpr static auto get(storage obj) {
            if constexpr (std::is_const_v<T>) {
                return static_cast<T*>(obj.const_pointer);
            } else if constexpr (std::is_object_v<T>) {
                return static_cast<T*>(obj.pointer);
            } else {
                return reinterpret_cast<T*>(obj.fun_pointer);
            }
        }
    };

    template <class Sig, class = typename _qual_fn_sig<Sig>::function>
    struct function_ref;

    template <class Sig, class R, class... Args>
    struct function_ref<Sig, R(Args...)> : function_ref_base {
        storage object;
        using fwd_t        = R(storage, param_t<Args>...);
        fwd_t* fun_pointer = nullptr;

        using signature = _qual_fn_sig<Sig>;
        template <class T>
        using cv = typename signature::template cv<T>;

        template <class T>
        using cvref = cv<T>&;

        template <class... T>
        constexpr static bool is_invocable_using
                = signature::template is_invocable_using<T...>;

        template <class F, class... T>
        constexpr static bool is_memfn_invocable_using
                = (is_invocable_using<F, T...>)&&(std::is_member_pointer_v<F>);

        // NOLINTBEGIN(google-explicit-constructor,hicpp-explicit-conversions,bugprone-forwarding-reference-overload,cppcoreguidelines-missing-std-forward)
        template <class F>
        requires std::is_function_v<F> && is_invocable_using<F>
        constexpr function_ref(F* callable) noexcept
                : object(callable), fun_pointer([](storage func, param_t<Args>... args) {
                      return std23::invoke_r<R>(
                              get<F>(func), std::forward<Args>(args)...);
                  }) {}

        template <class F, class T = remove_and_unwrap_reference_t<F>>
        requires is_not_self<F, function_ref> && is_invocable_using<cvref<T>>
        constexpr function_ref(F&& callable) noexcept
                : object(std::addressof(static_cast<T&>(callable))),
                  fun_pointer([](storage func, param_t<Args>... args) {
                      cvref<T> obj = *get<T>(func);
                      return std23::invoke_r<R>(obj, std::forward<Args>(args)...);
                  }) {}

        template <auto F>
        requires is_invocable_using<decltype(F)>
        constexpr function_ref(nontype_t<F>) noexcept
                : fun_pointer([](storage, param_t<Args>... args) {
                      return std23::invoke_r<R>(F, std::forward<Args>(args)...);
                  }) {}

        template <
                auto F, class U, class Ty = std::unwrap_reference_t<U>,
                class T = std::remove_reference_t<Ty>>
        requires std::is_lvalue_reference_v<Ty>
                         && is_invocable_using<decltype(F), cvref<T>>
        constexpr function_ref(nontype_t<F>, U&& obj) noexcept
                : object(std::addressof(static_cast<Ty>(obj))),
                  fun_pointer([](storage self, param_t<Args>... args) {
                      cvref<T> real_obj = *get<T>(self);
                      return std23::invoke_r<R>(F, real_obj, std::forward<Args>(args)...);
                  }) {}

        // NOLINTEND(google-explicit-constructor,hicpp-explicit-conversions,bugprone-forwarding-reference-overload,cppcoreguidelines-missing-std-forward)

        template <auto F, class T>
        constexpr function_ref(nontype_t<F>, cv<T>* obj) noexcept
        requires is_memfn_invocable_using<decltype(F), decltype(obj)>
                : object(obj), fun_pointer([](storage self, param_t<Args>... args) {
                      return std23::invoke_r<R>(
                              F, get<cv<T>>(self), std::forward<Args>(args)...);
                  }) {}

        constexpr R operator()(Args... args) const noexcept(signature::is_noexcept) {
            return fun_pointer(object, std::forward<Args>(args)...);
        }
    };

    template <class T>
    struct _adapt_signature;

    template <class F>
    requires std::is_function_v<F>
    struct _adapt_signature<F*> {
        using type = F;
    };

    template <class Fp>
    using adapt_signature_t = typename _adapt_signature<Fp>::type;

    template <class T>
    struct _drop_first_arg_to_invoke;

    template <class R, class T, class... Args>
    struct _drop_first_arg_to_invoke<R (*)(T, Args...)> {
        using type = R(Args...);
    };

    template <class R, class T, class... Args>
    struct _drop_first_arg_to_invoke<R (*)(T, Args...) noexcept> {
        using type = R(Args...);
    };

    template <class T, class Cls>
    requires std::is_object_v<T>
    struct _drop_first_arg_to_invoke<T Cls::*> {
        using type = T();
    };

    template <class T, class Cls>
    requires std::is_function_v<T>
    struct _drop_first_arg_to_invoke<T Cls::*> {
        using type = T;
    };

    template <class Fp>
    using drop_first_arg_to_invoke_t = typename _drop_first_arg_to_invoke<Fp>::type;

    template <class F>
    requires std::is_function_v<F>
    function_ref(F*) -> function_ref<F>;

    template <auto V>
    function_ref(nontype_t<V>) -> function_ref<adapt_signature_t<decltype(V)>>;

    template <auto V>
    function_ref(nontype_t<V>, auto)
            -> function_ref<drop_first_arg_to_invoke_t<decltype(V)>>;
}    // namespace std23
