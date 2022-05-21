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

using std::ios;
using std::istream;
using std::numeric_limits;
using std::ostream;
using std::streamsize;
using std::stringstream;

template <>
size_t moduled_snkrle::PadMaskBits = 1U;

class snkrle_internal {
public:
    static void decode(istream& Source, ostream& Dest) {
        size_t Size = BigEndian::Read2(Source);
        if (Size == 0) {
            return;
        }
        uint8_t curr = Read1(Source);
        Write1(Dest, curr);
        Size--;
        while (Size > 0) {
            uint8_t next = Read1(Source);
            Write1(Dest, next);
            Size--;
            if (curr == next) {
                // RLE marker. Get repeat count.
                size_t Count = Read1(Source);
                for (size_t ii = 0; ii < Count; ii++) {
                    Write1(Dest, next);
                }
                Size -= Count;
                if (Count == 255 && Size > 0) {
                    curr = Read1(Source);
                    Write1(Dest, next);
                    Size--;
                }
            } else {
                curr = next;
            }
        }
    }

    static void encode(istream& Source, ostream& Dest) {
        auto position = Source.tellg();
        Source.ignore(numeric_limits<streamsize>::max());
        std::streampos Size = Source.gcount();
        Source.seekg(position);
        BigEndian::Write2(Dest, static_cast<uint16_t>(Size));
        uint8_t curr = Read1(Source);
        while (Source.good()) {
            Write1(Dest, curr);
            uint8_t next = Read1(Source);
            if (!Source.good()) {
                break;
            }
            if (next == curr) {
                Write1(Dest, next);
                size_t Count = 0;
                curr         = Read1(Source);
                while (Source.good() && next == curr && Count < 255) {
                    Count++;
                    curr = Read1(Source);
                }
                Write1(Dest, Count & std::numeric_limits<uint8_t>::max());
            } else {
                curr = next;
            }
        }
    }
};

bool snkrle::decode(istream& Source, ostream& Dest) {
    auto const   Location = Source.tellg();
    stringstream input(ios::in | ios::out | ios::binary);
    extract(Source, input);

    snkrle_internal::decode(input, Dest);
    Source.seekg(Location + input.tellg());
    return true;
}

bool snkrle::encode(ostream& Dest, uint8_t const* data, size_t const Size) {
    stringstream source(ios::in | ios::out | ios::binary);
    source.write(reinterpret_cast<char const*>(data), static_cast<std::streamsize>(Size));
    source.seekg(0);
    snkrle_internal::encode(source, Dest);
    return true;
}
