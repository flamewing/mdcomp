/*
 * Copyright (C) Flamewing 2025 <flamewing.sonic@gmail.com>
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

#ifndef LIB_UNREACHABLE_HH
#define LIB_UNREACHABLE_HH

namespace utils {
#if defined(_MSC_VER)
#    define INLINE [[msvc::forceinline]] inline
#elif defined(__GNUG__)
#    define INLINE [[gnu::always_inline]] inline
#else
#    define INLINE inline
#endif
#if defined(__cpp_lib_unreachable) && __cpp_lib_unreachable >= 202202L
#    include <utility>
    using std::unreachable;
#else
    [[noreturn]] INLINE void unreachable() {
        // Uses compiler specific extensions if possible.
        // Even if no extension is used, undefined behavior is still raised by
        // an empty function body and the noreturn attribute.
#    if defined(_MSC_VER) && !defined(__clang__)    // MSVC
        __assume(false);
#    else                                           // GCC, Clang
        __builtin_unreachable();
#    endif
    }
#endif

    INLINE void assume(bool condition) {
        // Uses compiler specific extensions if possible.
        // Even if no extension is used, undefined behavior is still raised by
        // an empty function body and the noreturn attribute.
#if defined(_MSC_VER) && !defined(__clang__)    // MSVC
        __assume(condition);
#else    // GCC, Clang
        if (!condition) {
            __builtin_unreachable();
        }
#endif
    }

#undef INLINE
}    // namespace utils

#endif    // LIB_UNREACHABLE_HH
