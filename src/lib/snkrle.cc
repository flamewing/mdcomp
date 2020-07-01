/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
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

#include <mdcomp/bigendian_io.hh>
#include <mdcomp/ignore_unused_variable_warning.hh>
#include <mdcomp/snkrle.hh>

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
    static void decode(istream& Src, ostream& Dst) {
        size_t Size = BigEndian::Read2(Src);
        if (Size == 0) {
            return;
        }
        uint8_t cc = Read1(Src);
        Write1(Dst, cc);
        Size--;
        while (Size > 0) {
            uint8_t nc = Read1(Src);
            Write1(Dst, nc);
            Size--;
            if (cc == nc) {
                // RLE marker. Get repeat count.
                size_t Count = Read1(Src);
                for (size_t ii = 0; ii < Count; ii++) {
                    Write1(Dst, nc);
                }
                Size -= Count;
                if (Count == 255 && Size > 0) {
                    cc = Read1(Src);
                    Write1(Dst, nc);
                    Size--;
                }
            } else {
                cc = nc;
            }
        }
    }

    static void encode(istream& Src, ostream& Dst) {
        size_t pos = Src.tellg();
        Src.ignore(numeric_limits<streamsize>::max());
        size_t Size = Src.gcount();
        Src.seekg(pos);
        BigEndian::Write2(Dst, Size);
        uint8_t cc = Read1(Src);
        while (Src.good()) {
            Write1(Dst, cc);
            uint8_t nc = Read1(Src);
            if (!Src.good()) {
                break;
            }
            if (nc == cc) {
                Write1(Dst, nc);
                size_t Count = 0;
                cc           = Read1(Src);
                while (Src.good() && nc == cc && Count < 255) {
                    Count++;
                    cc = Read1(Src);
                }
                Write1(Dst, Count);
            } else {
                cc = nc;
            }
        }
    }
};

bool snkrle::decode(istream& Src, ostream& Dst) {
    size_t const Location = Src.tellg();
    stringstream in(ios::in | ios::out | ios::binary);
    extract(Src, in);

    snkrle_internal::decode(in, Dst);
    Src.seekg(Location + in.tellg());
    return true;
}

bool snkrle::encode(ostream& Dst, uint8_t const* data, size_t const Size) {
    stringstream Src(ios::in | ios::out | ios::binary);
    Src.write(reinterpret_cast<char const*>(data), Size);
    Src.seekg(0);
    snkrle_internal::encode(Src, Dst);
    return true;
}
