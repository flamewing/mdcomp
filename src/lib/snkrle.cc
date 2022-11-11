/*
 * Copyright (C) Flamewing 2017 <flamewing.sonic@gmail.com>
 * Loosely based on code by snkenjoi
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

#include "mdcomp/snkrle.hh"

#include "mdcomp/bigendian_io.hh"
#include "mdcomp/ignore_unused_variable_warning.hh"

#include <istream>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>

template <>
size_t moduled_snkrle::pad_mask_bits = 1U;

class snkrle_internal {
    using snkrle_output_iterator = endian_output_iterator<source_endian, uint8_t>;

public:
    static void decode(std::istream& source, std::ostream& dest) {
        ptrdiff_t size = big_endian::read2(source);
        if (size == 0) {
            return;
        }
        uint8_t curr = read1(source);
        write1(dest, curr);
        size--;
        while (size > 0) {
            uint8_t const next = read1(source);
            write1(dest, next);
            size--;
            if (curr == next) {
                // RLE marker. Get repeat count.
                ptrdiff_t const count = read1(source);
                std::ranges::fill_n(snkrle_output_iterator(dest), count, next);
                size -= count;
                if (count == 255 && size > 0) {
                    curr = read1(source);
                    write1(dest, next);
                    size--;
                }
            } else {
                curr = next;
            }
        }
    }

    static void encode(std::istream& source, std::ostream& dest) {
        auto position = source.tellg();
        source.ignore(std::numeric_limits<std::streamsize>::max());
        std::streampos const size = source.gcount();
        source.seekg(position);
        big_endian::write2(dest, static_cast<uint16_t>(size));
        uint8_t curr = read1(source);
        while (source.good()) {
            write1(dest, curr);
            uint8_t const next = read1(source);
            if (!source.good()) {
                break;
            }
            if (next == curr) {
                write1(dest, next);
                size_t count = 0;
                curr         = read1(source);
                while (source.good() && next == curr && count < 255) {
                    count++;
                    curr = read1(source);
                }
                write1(dest, count & std::numeric_limits<uint8_t>::max());
            } else {
                curr = next;
            }
        }
    }
};

bool snkrle::decode(std::istream& source, std::ostream& dest) {
    auto const        location = source.tellg();
    std::stringstream input(std::ios::in | std::ios::out | std::ios::binary);
    extract(source, input);

    snkrle_internal::decode(input, dest);
    source.seekg(location + input.tellg());
    return true;
}

bool snkrle::encode(std::ostream& dest, std::span<uint8_t const> data) {
    std::stringstream source(std::ios::in | std::ios::out | std::ios::binary);
    source.write(reinterpret_cast<char const*>(data.data()), std::ssize(data));
    source.seekg(0);
    snkrle_internal::encode(source, dest);
    return true;
}
