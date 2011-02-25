/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * Kosinski encoder/decoder
 * Copyright (C) Flamewing 2011 <flamewing.sonic@gmail.com>
 * Copyright (C) 2002-2004 The KENS Project Development Team
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

#include <istream>
#include <ostream>
#include <string>
#include <sstream>
#include <algorithm>

#include "kosinski.h"
#include "bigendian_io.h"
#include "bitstream.h"

void kosinski::decode_internal(std::istream& in, std::iostream& Dst, size_t &DecBytes)
{
	ibitstream<unsigned short, littleendian<unsigned short> > bits(in);

	while (true)
	{
		if (bits.pop())
		{
			Write1(Dst, Read1(in));
			++DecBytes;
		}
		else
		{
			// Count and Offest
			size_t Count = 0;
			std::streamoff Offset = 0;

			if (bits.pop())
			{
				unsigned char Low = Read1(in), High = Read1(in);

				Count = (size_t)(High & 0x07);

				if (!Count)
				{
					Count = Read1(in);
					if (!Count)
						break;
					else if (Count == 1)
						continue;
				}
				else
					Count += 1;

				Offset = (~((std::streamoff)0x1FFF)) | ((std::streamoff)(0xF8 & High) << 5) | (std::streamoff)Low;
			}
			else
			{
				unsigned char Low  = bits.pop(),
				              High = bits.pop();

				Count = ((size_t)Low)*2 + ((size_t)High) + 1;

				Offset = Read1(in);
				Offset |= (~((std::streamoff)0xFF));
			}

			for (size_t i = 0; i <= Count; i++)
			{
				std::streampos Pointer = Dst.tellp();
				Dst.seekg(Pointer + Offset);
				unsigned char Byte = Read1(Dst);
				Dst.seekp(Pointer);
				Write1(Dst, Byte);
			}
			DecBytes += (Count + 1);
		}
	}
}

bool kosinski::decode(std::istream& Src, std::iostream& Dst,
                      std::streampos Location, bool Moduled)
{
	size_t DecBytes = 0;

	Src.seekg(0, std::ios::end);
	std::streamsize sz = std::streamsize(Src.tellg()) - Location;
	Src.seekg(Location);

	std::stringstream in(std::ios::in|std::ios::out|std::ios::binary);
	in << Src.rdbuf();

	// Pad to even length, for safety.
	if ((sz & 1) != 0)
		in.put(0x00);

	in.seekg(0);

	if (Moduled)
	{
		size_t FullSize = BigEndian::Read2(in);
		while (true)
		{
			decode_internal(in, Dst, DecBytes);
			if (DecBytes >= FullSize)
				break;
			while (in.peek() == 0)
				in.ignore(1);
		}
	}
	else
		decode_internal(in, Dst, DecBytes);
	
	return true;
}

static inline void push(obitstream<unsigned short, littleendian<unsigned short> >& bits,
                        unsigned short bit, std::ostream& Dst, std::string& Data)
{
	if (bits.push(bit))
	{
		Dst.write(Data.c_str(), Data.size());
		Data.clear();
	}
}

void kosinski::encode_internal(std::ostream& Dst, unsigned char const *&Buffer,
                               std::streamoff SlideWin, std::streamoff RecLen,
                               std::streamsize const BSize)
{
	obitstream<unsigned short, littleendian<unsigned short> > bits(Dst);
	bits.push(1);
	std::string Data;
	std::streamoff BPointer = 1, IOffset = 0;

	Data.clear();
	Write1(Data, Buffer[0]);

	do
	{
		// Count and Offest
		std::streamoff ICount = std::min(RecLen, BSize - BPointer),
		               imax = std::max(BPointer - SlideWin, (std::streamoff)0),
		               k = 1, i = BPointer - 1;

		do
		{
			std::streamoff j = 0;
			while (Buffer[i + j] == Buffer[BPointer + j])
				if (++j >= ICount)
					break;
			
			if (j > k)
			{
				k = j;
				IOffset = i;
			}
		} while (i-- > imax);

		ICount = k;

		if (ICount == 1)
		{
			push(bits, 1, Dst, Data);
			Write1(Data, Buffer[BPointer]);
		}
		else if ((ICount == 2) && (BPointer - IOffset > 256))
		{
			push(bits, 1, Dst, Data);
			Write1(Data, Buffer[BPointer]);
			--ICount;
		}
		else if ((ICount < 6) && (BPointer - IOffset <= 256))
		{
			push(bits, 0, Dst, Data);
			push(bits, 0, Dst, Data);
			push(bits, ((ICount-2) >> 1) & 1, Dst, Data);
			push(bits, (ICount-2) & 1, Dst, Data);
			Write1(Data, static_cast<unsigned char>(~(BPointer - IOffset - 1)));
		}
		else
		{
			push(bits, 0, Dst, Data);
			push(bits, 1, Dst, Data);

			unsigned short Off = static_cast<unsigned short>(BPointer - IOffset - 1);
			unsigned short Info = static_cast<unsigned short>(~((Off << 8) | (Off >> 5)) & 0xFFF8);
			if (ICount-2 < 8)
			{
				Info |= static_cast<unsigned char>(ICount - 2);
				BigEndian::Write2(Data, Info);
			}
			else
			{
				BigEndian::Write2(Data, Info);
				Write1(Data, static_cast<unsigned char>(ICount - 1));
			}
		}

		BPointer += ICount;
	} while (BPointer < BSize);

	push(bits, 0, Dst, Data);
	push(bits, 1, Dst, Data);

	Write1(Data, static_cast<unsigned char>(0x00));
	Write1(Data, static_cast<unsigned char>(0xF0));
	Write1(Data, static_cast<unsigned char>(0x00));

	bits.flush(true);
	Dst.write(Data.c_str(), Data.size());
}

bool kosinski::encode(std::istream& Src, std::ostream& Dst, std::streamoff SlideWin,
                      std::streamoff RecLen, bool Moduled)
{
	Src.seekg(0, std::ios::end);
	std::streamsize BSize = Src.tellg();
	Src.seekg(0);
	unsigned char * const Buffer = new unsigned char[BSize];
	unsigned char const *ptr = Buffer;
	Src.read((char *)ptr, BSize);

	if (Moduled)
	{
		if (BSize > 65535)
			return false;

		std::streamoff FullSize = BSize, CompBytes = 0;

		if (BSize > 0x1000)
			BSize = 0x1000;

		BigEndian::Write2(Dst, FullSize);

		while (true)
		{
			encode_internal(Dst, ptr, SlideWin, RecLen, BSize);
			std::streampos n = 16 - ((size_t(Dst.tellp()) - 2) % 16);
			
			if (n)
			{
				char const c = 0;
				Dst.write(&c, n);
			}
			
			CompBytes += BSize;
			ptr += BSize;

			if (CompBytes >= FullSize)
				break;

			BSize = std::min((std::streamoff)0x1000, FullSize - CompBytes);
		}
	}
	else
		encode_internal(Dst, ptr, SlideWin, RecLen, BSize);

	// Pad to even size.
	if ((Dst.tellp() & 1) != 0)
		Dst.put(0);
	
	delete [] Buffer;
	return true;
}
