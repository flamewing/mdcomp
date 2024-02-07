/*
 * Copyright (C) Flamewing 2016 <flamewing.sonic@gmail.com>
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

#ifndef LIB_IGNORE_UNUSED_VARIABLE_WARNING_HH
#define LIB_IGNORE_UNUSED_VARIABLE_WARNING_HH

template <typename... T>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
constexpr void ignore_unused_variable_warning(T&&...) {
    // This function is only meant to silence unused variable warnings.
}

#endif    // LIB_IGNORE_UNUSED_VARIABLE_WARNING_HH
