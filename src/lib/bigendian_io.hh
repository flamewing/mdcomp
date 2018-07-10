/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * Copyright (C) Flamewing 2011-2016 <flamewing.sonic@gmail.com>
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

#ifndef __LIB_BIGENDIAN_IO_H
#define __LIB_BIGENDIAN_IO_H

#include <iosfwd>
#include <iterator>
#include <string>

inline size_t Read1(std::istream &in) noexcept {
	size_t c = static_cast<uint8_t>(in.get());
	return c;
}

inline size_t Read1(int8_t const *& in) noexcept {
	size_t c = static_cast<uint8_t>(*in++);
	return c;
}

inline size_t Read1(uint8_t const *& in) noexcept {
	size_t c = *in++;
	return c;
}

inline size_t Read1(std::istream_iterator<uint8_t>& in) noexcept {
	size_t c = *in++;
	return c;
}

inline void Write1(std::ostream &out, size_t const c) noexcept {
	out.put(static_cast<int8_t>(c & 0xff));
}

inline void Write1(int8_t *&out, size_t const c) noexcept {
	*out++ = static_cast<int8_t>(c & 0xff);
}

inline void Write1(uint8_t *&out, size_t const c) noexcept {
	*out++ = static_cast<int8_t>(c & 0xff);
}

inline void Write1(std::string &out, size_t const c) noexcept {
	out.push_back(static_cast<int8_t>(c & 0xff));
}

inline void Write1(std::ostream_iterator<uint8_t>&out, size_t const c) noexcept {
	*out++ = static_cast<int8_t>(c & 0xff);
}

struct BigEndian {
	template <typename T>
	static inline size_t Read2(T &in) noexcept {
		size_t c = Read1(in) << 8;
		c |= Read1(in);
		return c;
	}

	template <typename T>
	static inline size_t Read4(T &in) noexcept {
		size_t c = Read1(in) << 24;
		c |= Read1(in) << 16;
		c |= Read1(in) << 8;
		c |= Read1(in);
		return c;
	}

	template <typename T, size_t N>
	static inline size_t ReadN(T &in) noexcept {
		size_t c = 0;
		for (size_t i = 0; i < N; i++) {
			c = (c << 8) | Read1(in);
		}
		return c;
	}

	template <typename T>
	static inline void Write2(T &out, size_t const c) noexcept {
		Write1(out, (c & 0xff00) >> 8);
		Write1(out, c & 0xff);
	}

	template <typename T>
	static inline void Write4(T &out, size_t const c) noexcept {
		Write1(out, (c & 0xff000000) >> 24);
		Write1(out, (c & 0x00ff0000) >> 16);
		Write1(out, (c & 0x0000ff00) >> 8);
		Write1(out, (c & 0x000000ff));
	}

	template <typename T, size_t N>
	static inline void WriteN(T &out, size_t const c) noexcept {
		for (size_t i = 0; i < 8 * N; i += 8) {
			Write1(out, (c >> (8 * (N - 1) - i)) & 0xff);
		}
	}
};

struct LittleEndian {
	template <typename T>
	static inline size_t Read2(T &in) noexcept {
		size_t c = Read1(in);
		c |= Read1(in) << 8;
		return c;
	}

	template <typename T>
	static inline size_t Read4(T &in) noexcept {
		size_t c = Read1(in);
		c |= Read1(in) << 8;
		c |= Read1(in) << 16;
		c |= Read1(in) << 24;
		return c;
	}

	template <typename T, size_t N>
	static inline size_t ReadN(T &in) noexcept {
		size_t c = 0;
		for (size_t i = 0; i < 8 * N; i += 8) {
			c = c | (Read1(in) << i);
		}
		return c;
	}

	template <typename T>
	static inline void Write2(T &out, size_t const c) noexcept {
		Write1(out, c & 0xff);
		Write1(out, (c & 0xff00) >> 8);
	}

	template <typename T>
	static inline void Write4(T &out, size_t const c) noexcept {
		Write1(out, (c & 0x000000ff));
		Write1(out, (c & 0x0000ff00) >> 8);
		Write1(out, (c & 0x00ff0000) >> 16);
		Write1(out, (c & 0xff000000) >> 24);
	}

	template <typename T, size_t N>
	static inline void WriteN(T &out, size_t const c) noexcept {
		for (size_t i = 0; i < 8 * N; i += 8) {
			Write1(out, (c >> i) & 0xff);
		}
	}
};

#endif
