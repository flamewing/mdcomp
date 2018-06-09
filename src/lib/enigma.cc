/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * Copyright (C) Flamewing 2011-2016 <flamewing.sonic@gmail.com>
 * Copyright (C) 2002-2004 The KENS Project Development Team
 * Copyright (C) 2002-2003 Roger Sanders (AKA Nemesis)
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

#include <algorithm>
#include <iostream>
#include <istream>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "enigma.h"
#include "bigendian_io.h"
#include "bitstream.h"

using namespace std;

using EniIBitstream = ibitstream<uint16_t, true>;
using EniOBitstream = obitstream<uint16_t>;

// Pure virtual base class.
class base_flag_io {
public:
	base_flag_io() noexcept = default;
	base_flag_io(base_flag_io const &other) noexcept = default;
	base_flag_io(base_flag_io &&other) noexcept = default;
	base_flag_io &operator=(base_flag_io const &other) noexcept = default;
	base_flag_io &operator=(base_flag_io &&other) noexcept = default;
	virtual ~base_flag_io() = default;
	static unique_ptr<base_flag_io> create(size_t const n);
	virtual unsigned short read_bitfield(EniIBitstream &bits) const = 0;
	virtual void write_bitfield(EniOBitstream &bits, unsigned short const flags) const = 0;
};

// Templated for blazing speed.
template<int const N>
class flag_io : public base_flag_io {
public:
	unsigned short read_bitfield(EniIBitstream &bits) const override {
		unsigned short flags = 0;
		if ((N & (1 << 4)) != 0) {
			flags |= bits.pop() << 15;
		}
		if ((N & (1 << 3)) != 0) {
			flags |= bits.pop() << 14;
		}
		if ((N & (1 << 2)) != 0) {
			flags |= bits.pop() << 13;
		}
		if ((N & (1 << 1)) != 0) {
			flags |= bits.pop() << 12;
		}
		if ((N & (1 << 0)) != 0) {
			flags |= bits.pop() << 11;
		}
		return flags;
	}
	void write_bitfield(EniOBitstream &bits, unsigned short const flags) const override {
		if ((N & (1 << 4)) != 0) {
			bits.push((flags >> 15) & 1);
		}
		if ((N & (1 << 3)) != 0) {
			bits.push((flags >> 14) & 1);
		}
		if ((N & (1 << 2)) != 0) {
			bits.push((flags >> 13) & 1);
		}
		if ((N & (1 << 1)) != 0) {
			bits.push((flags >> 12) & 1);
		}
		if ((N & (1 << 0)) != 0) {
			bits.push((flags >> 11) & 1);
		}
	}
};

// Selects the appropriate template instance to be used.
unique_ptr<base_flag_io> base_flag_io::create(size_t const n) {
	switch ((n & 0x1F)) {
		case 0x00:
			return make_unique<flag_io<0x00>>();
		case 0x01:
			return make_unique<flag_io<0x01>>();
		case 0x02:
			return make_unique<flag_io<0x02>>();
		case 0x03:
			return make_unique<flag_io<0x03>>();
		case 0x04:
			return make_unique<flag_io<0x04>>();
		case 0x05:
			return make_unique<flag_io<0x05>>();
		case 0x06:
			return make_unique<flag_io<0x06>>();
		case 0x07:
			return make_unique<flag_io<0x07>>();
		case 0x08:
			return make_unique<flag_io<0x08>>();
		case 0x09:
			return make_unique<flag_io<0x09>>();
		case 0x0a:
			return make_unique<flag_io<0x0a>>();
		case 0x0b:
			return make_unique<flag_io<0x0b>>();
		case 0x0c:
			return make_unique<flag_io<0x0c>>();
		case 0x0d:
			return make_unique<flag_io<0x0d>>();
		case 0x0e:
			return make_unique<flag_io<0x0e>>();
		case 0x0f:
			return make_unique<flag_io<0x0f>>();
		case 0x10:
			return make_unique<flag_io<0x10>>();
		case 0x11:
			return make_unique<flag_io<0x11>>();
		case 0x12:
			return make_unique<flag_io<0x12>>();
		case 0x13:
			return make_unique<flag_io<0x13>>();
		case 0x14:
			return make_unique<flag_io<0x14>>();
		case 0x15:
			return make_unique<flag_io<0x15>>();
		case 0x16:
			return make_unique<flag_io<0x16>>();
		case 0x17:
			return make_unique<flag_io<0x17>>();
		case 0x18:
			return make_unique<flag_io<0x18>>();
		case 0x19:
			return make_unique<flag_io<0x19>>();
		case 0x1a:
			return make_unique<flag_io<0x1a>>();
		case 0x1b:
			return make_unique<flag_io<0x1b>>();
		case 0x1c:
			return make_unique<flag_io<0x1c>>();
		case 0x1d:
			return make_unique<flag_io<0x1d>>();
		case 0x1e:
			return make_unique<flag_io<0x1e>>();
		case 0x1f:
			return make_unique<flag_io<0x1f>>();
		default:
			cerr << "Divide By Cucumber Error. Please Reinstall Universe And Reboot." << endl;
			return nullptr;
	}
}

// Blazing fast function that gives the index of the MSB.
static inline unsigned char slog2(unsigned short v) {
	unsigned char r; // result of slog2(v) will go here
	unsigned char shift;

	r = static_cast<int>(v > 0xFF) << 3;
	v >>= r;
	shift = static_cast<int>(v > 0xF) << 2;
	v >>= shift;
	r |= shift;
	shift = static_cast<int>(v > 0x3) << 1;
	v >>= shift;
	r |= shift;
	r |= (v >> 1);
	return r;
}

// Comparison functor, see below.
struct Compare_count {
	bool operator()(pair<unsigned short const, size_t> &it1,
	                pair<unsigned short const, size_t> &it2) {
		return (it1.second < it2.second);
	}
};

// This flushes (if needed) the contents of the inlined data buffer.
static inline void flush_buffer(vector<unsigned short> &buf,
                                EniOBitstream &bits,
                                unique_ptr<base_flag_io> &mask,
                                unsigned short const packet_length) {
	if (buf.empty()) {
		return;
	}

	bits.write(0x70 | ((buf.size() - 1) & 0xf), 7);
	for (const auto v : buf) {
		mask->write_bitfield(bits, v);
		bits.write(v & 0x7ff, packet_length);
	}
	buf.clear();
}

template<>
size_t moduled_enigma::PadMaskBits = 1u;

class enigma_internal {
public:
	static void decode(std::istream &Src, std::ostream &Dst) {
		stringstream in(ios::in | ios::out | ios::binary);
		in << Src.rdbuf();
		in.seekg(0);

		// Read header.
		size_t const packet_length = Read1(in);
		auto mask = base_flag_io::create(Read1(in));
		size_t incrementing_value = BigEndian::Read2(in);
		size_t const common_value = BigEndian::Read2(in);

		ibitstream<unsigned short, true> bits(in);
		constexpr static int const modeDeltaLUT[] = {0, 1, -1};

		// Lets put in a safe termination condition here.
		while (in.good()) {
			if (bits.pop() != 0u) {
				int const mode = bits.read(2);
				switch (mode) {
					case 2:
					case 1:
					case 0: {
						size_t const cnt = bits.read(4) + 1;
						unsigned short const flags = mask->read_bitfield(bits);
						unsigned short outv = bits.read(packet_length);
						outv |= flags;

						for (size_t i = 0; i < cnt; i++) {
							BigEndian::Write2(Dst, outv);
							outv += modeDeltaLUT[mode];
						}
						break;
					}
					case 3: {
						size_t const cnt = bits.read(4);
						// This marks decompression as being done.
						if (cnt == 0x0F) {
							return;
						}

						for (size_t i = 0; i <= cnt; i++) {
							unsigned short flags = mask->read_bitfield(bits),
								           outv  = bits.read(packet_length);
							BigEndian::Write2(Dst, outv | flags);
						}
						break;
					}
				}
			} else {
				if (bits.pop() == 0u) {
					size_t const cnt = bits.read(4) + 1;
					for (size_t i = 0; i < cnt; i++) {
						BigEndian::Write2(Dst, incrementing_value++);
					}
				} else {
					size_t const cnt = bits.read(4) + 1;
					for (size_t i = 0; i < cnt; i++) {
						BigEndian::Write2(Dst, common_value);
					}
				}
			}
		}
	}

	static void encode(std::istream &Src, std::ostream &Dst) {
		// To unpack source into 2-byte words.
		vector<unsigned short> unpack;
		// Frequency map.
		map<unsigned short, size_t> counts;
		// Presence map.
		set<unsigned short> elems;

		// Unpack source into array. Along the way, build frequency and presence maps.
		unsigned short maskval = 0;
		Src.clear();
		Src.seekg(0);
		while (true) {
			unsigned short v = BigEndian::Read2(Src);
			if (!Src.good()) {
				break;
			}
			maskval |= v;
			counts[v] += 1;
			elems.insert(v);
			unpack.push_back(v);
		}

		auto mask = base_flag_io::create(maskval >> 11);
		unsigned short const packet_length = slog2(maskval & 0x7ff) + 1;

		// Find the most common 2-byte value.
		Compare_count cmp;
		auto high = max_element(counts.begin(), counts.end(), cmp);
		unsigned short const common_value = high->first;
		// No longer needed.
		counts.clear();

		// Find incrementing (not neccessarily contiguous) runs.
		// The original algorithm does this for all 65536 2-byte words, while
		// this version only checks the 2-byte words actually in the file.
		map<unsigned short, size_t> runs;
		for (auto next : elems) {
			auto val = runs.emplace(next, 0).first;
			for (auto & elem : unpack) {
				if (elem == next) {
					next++;
					val->second += 1;
				}
			}
		}
		// No longer needed.
		elems.clear();

		// Find the starting 2-byte value with the longest incrementing run.
		auto incr = max_element(runs.begin(), runs.end(), cmp);
		unsigned short incrementing_value = incr->first;
		// No longer needed.
		runs.clear();

		// Output header.
		Write1(Dst, packet_length);
		Write1(Dst, maskval >> 11);
		BigEndian::Write2(Dst, incrementing_value);
		BigEndian::Write2(Dst, common_value);

		// Time now to compress the file.
		EniOBitstream bits(Dst);
		vector<unsigned short> buf;
		size_t pos = 0;
		while (pos < unpack.size()) {
			unsigned short const v = unpack[pos];
			if (v == incrementing_value) {
				flush_buffer(buf, bits, mask, packet_length);
				unsigned short next = v + 1;
				size_t cnt = 0;
				for (size_t i = pos + 1; i < unpack.size() && cnt < 0xf; i++) {
					if (next != unpack[i]) {
						break;
					}
					next++;
					cnt++;
				}
				bits.write(0x00 | cnt, 6);
				incrementing_value = next;
				pos += cnt;
			} else if (v == common_value) {
				flush_buffer(buf, bits, mask, packet_length);
				unsigned short next = v;
				size_t cnt = 0;
				for (size_t i = pos + 1; i < unpack.size() && cnt < 0xf; i++) {
					if (next != unpack[i]) {
						break;
					}
					cnt++;
				}
				bits.write(0x10 | cnt, 6);
				pos += cnt;
			} else {
				unsigned short next = unpack[pos + 1];
				int delta = int(next) - int(v);
				if (pos + 1 < unpack.size() && next != incrementing_value &&
					    (delta == -1 || delta == 0 || delta == 1)) {
					flush_buffer(buf, bits, mask, packet_length);
					size_t cnt = 1;
					next += delta;
					for (size_t i = pos + 2; i < unpack.size() && cnt < 0xf; i++) {
						if (next != unpack[i] || next == incrementing_value) {
							break;
						}
						next += delta;
						cnt++;
					}

					if (delta == -1) {
						delta = 2;
					}

					delta = ((delta | 4) << 4);
					bits.write(delta | cnt, 7);
					mask->write_bitfield(bits, v);
					bits.write(v & 0x7ff, packet_length);
					pos += cnt;
				} else {
					if (buf.size() >= 0xf) {
						flush_buffer(buf, bits, mask, packet_length);
					}

					buf.push_back(v);
				}
			}
			pos++;
		}

		flush_buffer(buf, bits, mask, packet_length);

		// Terminator.
		bits.write(0x7f, 7);
		bits.flush();
	}
};

bool enigma::decode(istream &Src, ostream &Dst) {
	enigma_internal::decode(Src, Dst);
	return true;
}

bool enigma::encode(istream &Src, ostream &Dst) {
	enigma_internal::encode(Src, Dst);
	return true;
}

bool enigma::encode(std::ostream &Dst, unsigned char const *data, size_t const Size) {
	stringstream Src(ios::in | ios::out | ios::binary);
	Src.write(reinterpret_cast<char const*>(data), Size);
	Src.seekg(0);
	return encode(Src, Dst);
}
