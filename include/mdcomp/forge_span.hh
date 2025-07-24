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

#ifndef LIB_FORGE_SPAN_HH
#define LIB_FORGE_SPAN_HH

#include <concepts>
#include <cstddef>
#include <iterator>
#include <span>

// Workaround for the single dumbest most useless warning in the world. This is literally
// the whole point of this constructor: to bundle a C-style array plus size into a
// std::span for safe usage.
// They might as well reword the warning to say "This constructor is doing exactly what
// you want, but we are going to warn about it anyway. And make you do dumb workarounds to
// avoid the warning."
template <typename T>
constexpr auto unsafe_forge_span(T* pointer, size_t size) {
#ifdef __clang__
#    pragma clang unsafe_buffer_usage begin
#endif
    return std::span(pointer, size);
#ifdef __clang__
#    pragma clang unsafe_buffer_usage end
#endif
}

template <std::contiguous_iterator T>
constexpr auto unsafe_forge_span(T iter, ptrdiff_t size) {
#ifdef __clang__
#    pragma clang unsafe_buffer_usage begin
#endif
    return std::span(iter, std::next(iter, size));
#ifdef __clang__
#    pragma clang unsafe_buffer_usage end
#endif
}

template <typename T>
constexpr auto unsafe_forge_span(T* pointer, std::signed_integral auto size) {
    return unsafe_forge_span(pointer, static_cast<size_t>(size));
}

template <std::contiguous_iterator T>
constexpr auto unsafe_forge_span(T iter, std::unsigned_integral auto size) {
    return unsafe_forge_span(iter, static_cast<ptrdiff_t>(size));
}

#endif    // LIB_FORGE_SPAN_HH
