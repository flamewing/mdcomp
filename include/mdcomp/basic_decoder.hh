/*
 * Copyright (C) Flamewing 2016-2025 <flamewing.sonic@gmail.com>
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with main.c; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor Boston, MA 02110-1301,  USA
 */

#ifndef LIB_BASIC_DECODER_H
#define LIB_BASIC_DECODER_H

#include "mdcomp/stream_utils.hh"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <vector>

enum class pad_mode : uint8_t {
    dont_pad,
    pad_even
};

template <typename Format, pad_mode Pad, typename... Args>
class basic_decoder {
    basic_decoder() = default;
    friend Format;

public:
    static bool encode(std::istream& source, std::ostream& dest, Args... args);
    static void extract(std::istream& source, std::iostream& dest);
};

template <typename T, size_t Align = alignof(std::max_align_t)>
struct aligned_allocator : std::allocator<T> {
    static_assert(
            Align >= alignof(T),
            "Beware that types like int have minimum alignment requirements "
            "or access will result in crashes.");

public:
    using std::allocator<T>::allocator;

    template <typename U>
    struct rebind {
        using other = aligned_allocator<U, Align>;
    };

    template <typename U>
    explicit constexpr aligned_allocator(aligned_allocator<U, Align> const&) noexcept {}

    [[nodiscard]] T* allocate(std::size_t count) {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::bad_array_new_length();
        }

        auto const size_bytes = count * sizeof(T);
        return static_cast<T*>(::operator new[](size_bytes, std::align_val_t{Align}));
    }

    void deallocate(T* pointer, [[maybe_unused]] std::size_t count_bytes) {
        ignore_unused_variable_warning(count_bytes);
        ::operator delete[](pointer, std::align_val_t{Align});
    }
};

template <typename Format, pad_mode Pad, typename... Args>
bool basic_decoder<Format, Pad, Args...>::encode(
        std::istream& source, std::ostream& dest, Args... args) {
    using aligned_alloc = aligned_allocator<char, alignof(std::max_align_t)>;
    using cont_t        = std::vector<char, aligned_alloc>;

    auto full_size = utils::size(source);
    auto padding   = Pad == pad_mode::pad_even ? 2U : 1U;
    auto data      = utils::read_from_bytes<cont_t>(source, full_size, padding);
    if (Format::encode(dest, data, args...)) {
        utils::pad_to_even(dest);
        return true;
    }
    return false;
}

template <typename Format, pad_mode Pad, typename... Args>
void basic_decoder<Format, Pad, Args...>::extract(
        std::istream& source, std::iostream& dest) {
    dest << source.rdbuf();
    utils::pad_to_even(dest);
    dest.seekg(0);
}

#endif    // LIB_MODULED_ADAPTOR_H
