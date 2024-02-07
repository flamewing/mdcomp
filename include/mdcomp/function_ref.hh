#pragma once

#include <cassert>
#include <functional>
#include <type_traits>
#include <utility>

namespace std23 {

    // freestanding
    template <auto V>
    struct nontype_t {
        explicit nontype_t() = default;
    };

    // freestanding
    template <auto V>
    constexpr inline nontype_t<V> nontype{};

    using std::in_place_type;
    using std::in_place_type_t;
    using std::initializer_list;
    using std::nullptr_t;

    // freestanding
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
    using param_t = std::invoke_result_t<decltype(select_param_type<T>)>::type;

    template <class T, template <class...> class>
    constexpr inline bool looks_nullable_to_impl = std::is_member_pointer_v<T>;

    template <class F, template <class...> class Self>
    constexpr inline bool looks_nullable_to_impl<F*, Self> = std::is_function_v<F>;

    template <class... S, template <class...> class Self>
    constexpr inline bool looks_nullable_to_impl<Self<S...>, Self> = true;

    template <class S, template <class...> class Self>
    constexpr inline bool looks_nullable_to
            = looks_nullable_to_impl<std::remove_cvref_t<S>, Self>;

    template <class T>
    constexpr inline bool is_not_nontype_t = true;
    template <auto f>
    constexpr inline bool is_not_nontype_t<nontype_t<f>> = false;

    template <class T>
    struct adapt_signature;

    template <class F>
    requires std::is_function_v<F>
    struct adapt_signature<F*> {
        using type = F;
    };

    template <class Fp>
    using adapt_signature_t = adapt_signature<Fp>::type;

    template <class S>
    struct not_qualifying_this {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...)> {
        using type = R(Args...);
    };

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) noexcept> {
        using type = R(Args...) noexcept;
    };

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) const> : not_qualifying_this<R(Args...)> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) volatile> : not_qualifying_this<R(Args...)> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) const volatile>
            : not_qualifying_this<R(Args...)> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...)&> : not_qualifying_this<R(Args...)> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) const&> : not_qualifying_this<R(Args...)> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) volatile&> : not_qualifying_this<R(Args...)> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) const volatile&>
            : not_qualifying_this<R(Args...)> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) &&> : not_qualifying_this<R(Args...)> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) const&&> : not_qualifying_this<R(Args...)> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) volatile&&> : not_qualifying_this<R(Args...)> {
    };

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) const volatile&&>
            : not_qualifying_this<R(Args...)> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) const noexcept>
            : not_qualifying_this<R(Args...) noexcept> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) volatile noexcept>
            : not_qualifying_this<R(Args...) noexcept> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) const volatile noexcept>
            : not_qualifying_this<R(Args...) noexcept> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) & noexcept>
            : not_qualifying_this<R(Args...) noexcept> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) const & noexcept>
            : not_qualifying_this<R(Args...) noexcept> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) volatile & noexcept>
            : not_qualifying_this<R(Args...) noexcept> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) const volatile & noexcept>
            : not_qualifying_this<R(Args...) noexcept> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) && noexcept>
            : not_qualifying_this<R(Args...) noexcept> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) const && noexcept>
            : not_qualifying_this<R(Args...) noexcept> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) volatile && noexcept>
            : not_qualifying_this<R(Args...) noexcept> {};

    template <class R, class... Args>
    struct not_qualifying_this<R(Args...) const volatile && noexcept>
            : not_qualifying_this<R(Args...) noexcept> {};

    template <class F, class T>
    struct drop_first_arg_to_invoke;

    template <class T, class R, class G, class... Args>
    struct drop_first_arg_to_invoke<R (*)(G, Args...), T> {
        using type = R(Args...);
    };

    template <class T, class R, class G, class... Args>
    struct drop_first_arg_to_invoke<R (*)(G, Args...) noexcept, T> {
        using type = R(Args...) noexcept;
    };

    template <class T, class M, class G>
    requires std::is_object_v<M>
    struct drop_first_arg_to_invoke<M G::*, T> {
        using type = std::invoke_result_t<M G::*, T>();
    };

    template <class T, class M, class G>
    requires std::is_function_v<M>
    struct drop_first_arg_to_invoke<M G::*, T> : not_qualifying_this<M> {};

    template <class F, class T>
    using drop_first_arg_to_invoke_t = drop_first_arg_to_invoke<F, T>::type;

    template <class Sig>
    struct _qual_fn_sig;

    template <class R, class... Args>
    struct _qual_fn_sig<R(Args...)> {
        using function                    = R(Args...);
        constexpr static bool is_noexcept = false;

        template <class... T>
        constexpr static bool is_invocable_using
                = std::is_invocable_r_v<R, T..., Args...>;

        template <class T>
        using cv = T;
    };

    template <class R, class... Args>
    struct _qual_fn_sig<R(Args...) noexcept> {
        using function                    = R(Args...);
        constexpr static bool is_noexcept = true;

        template <class... T>
        constexpr static bool is_invocable_using
                = std::is_nothrow_invocable_r_v<R, T..., Args...>;

        template <class T>
        using cv = T;
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
                    : fun_pointer(std::bit_cast<decltype(fun_pointer)>(pointer_in)) {}
        };

        template <class T>
        constexpr static auto get(storage obj) {
            if constexpr (std::is_const_v<T>) {
                return static_cast<T*>(obj.const_pointer);
            } else if constexpr (std::is_object_v<T>) {
                return static_cast<T*>(obj.pointer);
            } else {
                return std::bit_cast<T*>(obj.fun_pointer);
            }
        }
    };

    // freestanding
    template <class Sig, class = typename _qual_fn_sig<Sig>::function>
    struct function_ref;

    // freestanding
    template <class Sig, class R, class... Args>
    struct function_ref<Sig, R(Args...)> : function_ref_base {
        using signature = _qual_fn_sig<Sig>;

        template <class T>
        using cv = signature::template cv<T>;
        template <class T>
        using cvref                = cv<T>&;
        constexpr static bool noex = signature::is_noexcept;

        template <class... T>
        constexpr static bool is_invocable_using
                = signature::template is_invocable_using<T...>;

        using fwd_t         = R(storage, param_t<Args>...) noexcept(false);
        fwd_t*  fun_pointer = nullptr;
        storage object;

        // NOLINTBEGIN(google-explicit-constructor,hicpp-explicit-conversions,cppcoreguidelines-missing-std-forward)
        template <class F>
        explicit(false) function_ref(F* callable) noexcept
        requires std::is_function_v<F> && is_invocable_using<F>
                : fun_pointer(
                        [](storage func, param_t<Args>... args) noexcept(noex) -> R {
                            if constexpr (std::is_void_v<R>) {
                                get<F>(func)(static_cast<decltype(args)>(args)...);
                            } else {
                                return get<F>(func)(static_cast<decltype(args)>(args)...);
                            }
                        }),
                  object(callable) {
            assert(callable != nullptr && "must reference a function");
        }

        template <class F, class T = std::remove_reference_t<F>>
        explicit(false) constexpr function_ref(F&& callable) noexcept
        requires(!std::is_same_v<std::remove_cvref_t<F>, function_ref>
                 && !std::is_member_pointer_v<T> && is_invocable_using<cvref<T>>)
                : fun_pointer(
                        [](storage func, param_t<Args>... args) noexcept(noex) -> R {
                            cvref<T> obj = *get<T>(func);
                            if constexpr (std::is_void_v<R>) {
                                obj(static_cast<decltype(args)>(args)...);
                            } else {
                                return obj(static_cast<decltype(args)>(args)...);
                            }
                        }),
                  object(std::addressof(callable)) {}

        template <class T>
        function_ref& operator=(T)
        requires(!std::is_same_v<std::remove_cvref_t<T>, function_ref>
                 && !std::is_pointer_v<T> && is_not_nontype_t<T>)
        = delete;

        template <auto f>
        explicit(false) constexpr function_ref(nontype_t<f>) noexcept
        requires is_invocable_using<decltype(f)>
                : fun_pointer([](storage, param_t<Args>... args) noexcept(noex) -> R {
                      return std23::invoke_r<R>(f, static_cast<decltype(args)>(args)...);
                  }) {
            using func_t = decltype(f);
            if constexpr (std::is_pointer_v<func_t> || std::is_member_pointer_v<func_t>) {
                static_assert(f != nullptr, "NTTP callable must be usable");
            }
        }

        template <auto f, class U, class T = std::remove_reference_t<U>>
        constexpr function_ref(nontype_t<f>, U&& obj) noexcept
        requires(!std::is_rvalue_reference_v<U &&>
                 && is_invocable_using<decltype(f), cvref<T>>)
                : fun_pointer(
                        [](storage self, param_t<Args>... args) noexcept(noex) -> R {
                            cvref<T> cobj = *get<T>(self);
                            return std23::invoke_r<R>(
                                    f, cobj, static_cast<decltype(args)>(args)...);
                        }),
                  object(std::addressof(obj)) {
            using func_t = decltype(f);
            if constexpr (std::is_pointer_v<func_t> || std::is_member_pointer_v<func_t>) {
                static_assert(f != nullptr, "NTTP callable must be usable");
            }
        }

        // NOLINTEND(google-explicit-constructor,hicpp-explicit-conversions,cppcoreguidelines-missing-std-forward)

        template <auto f, class T>
        constexpr function_ref(nontype_t<f>, cv<T>* obj) noexcept
        requires is_invocable_using<decltype(f), decltype(obj)>
                : fun_pointer(
                        [](storage self, param_t<Args>... args) noexcept(noex) -> R {
                            return std23::invoke_r<R>(
                                    f, get<cv<T>>(self),
                                    static_cast<decltype(args)>(args)...);
                        }),
                  object(obj) {
            using func_t = decltype(f);
            if constexpr (std::is_pointer_v<func_t> || std::is_member_pointer_v<func_t>) {
                static_assert(f != nullptr, "NTTP callable must be usable");
            }

            if constexpr (std::is_member_pointer_v<func_t>) {
                assert(obj != nullptr && "must reference an object");
            }
        }

        constexpr R operator()(Args... args) const noexcept(noex) {
            return fun_pointer(object, args...);
        }
    };

    template <class F>
    requires std::is_function_v<F>
    function_ref(F*) -> function_ref<F>;

    template <auto V>
    function_ref(nontype_t<V>) -> function_ref<adapt_signature_t<decltype(V)>>;

    template <auto V, class T>
    function_ref(nontype_t<V>, T&&)
            -> function_ref<drop_first_arg_to_invoke_t<decltype(V), T&>>;
}    // namespace std23
