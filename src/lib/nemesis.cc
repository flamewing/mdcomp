/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * Nemesis encoder/decoder
 * Copyright (C) Flamewing 2011 <flamewing.sonic@gmail.com>
 * Loosely based on code by Roger Sanders (AKA Nemesis) and William Sanders
 * (AKA Milamber)
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
#include <sstream>
#include <algorithm>
#include <functional>
#include <map>
#include <queue>
#include <string>
#include <vector>

#include "nemesis.h"
#include "bigendian_io.h"
#include "bitstream.h"

// This represents a nibble run of up to 7 repetitions of the starting nibble.
class nibble_run
{
private:
	unsigned char nibble;   // Nibble we are interested in.
	unsigned char count;	// How many times the nibble is repeated.
public:
	// Constructors.
	nibble_run() : nibble(0), count(0) {  }
	nibble_run(unsigned char n, unsigned char c) : nibble(n), count(c) {  }
	nibble_run(nibble_run const& other) : nibble(other.nibble), count(other.count) {  }
	nibble_run& operator=(nibble_run const& other)
	{
		if (this == &other)
			return *this;
		nibble = other.nibble;
		count = other.count;
		return *this;
	}
	// Sorting operator.
	bool operator<(nibble_run const& other) const
	{	return (nibble < other.nibble) || (nibble == other.nibble && count < other.count);	}
	// Getters/setters for all properties.
	unsigned char get_nibble() const
	{	return nibble;	}
	unsigned char get_count() const
	{	return count;	}
	void set_nibble(unsigned char tf)
	{	nibble = tf;	}
	void set_count(unsigned char tf)
	{	count = tf;		}
};

// Slightly based on code by Mark Nelson for Huffman encoding.
// http://marknelson.us/1996/01/01/priority-queues/
// This represents a node (leaf or branch) in the Huffman encoding tree.
class node
{
private:
	int weight;
	nibble_run value;
	node *child0;
	node *child1;
	// This goes through the tree, starting with the current node, generating
	// a map associating a nibble run with its code.
	void traverse_internal(size_t code, unsigned char nbits,
	                       std::map<nibble_run, std::pair<size_t, unsigned char> >& codemap) const
	{
		if ((nbits == 6 && code == 0x3F) || nbits >= 9)
			// This stops the recursion as we reach the %111111 pattern, which
			// indicates inline RLE in Nemesis format, or have exceeded the
			// allotted number of bits per nibble.
			return;
		else if (!is_leaf())
		{
			code <<= 1;
			nbits++;
			if (child0)
				child0->traverse_internal(code  , nbits, codemap);
			if (child1)
				child1->traverse_internal(code|1, nbits, codemap);
		}
		// Now if the child is worth it, put the code into the codemap.
		// This determination uses 2 bytes to specify the code, plus nbits bits
		// each time the code is used versus 13 bits (6 + 7) to specify the same
		// nibble run inlined, each time it is used.
		// It is slightly unoptimal, in that an additional byte might be needed
		// to specify the code (0x80 | nibble), but there is enough leeway to
		// dilute the extra byte in the figures below.
		else if ((nbits < 6 && weight > 1) || (nbits < 8 && weight > 2) || weight > 3)
			codemap[value] = std::pair<size_t, unsigned char>(code, nbits);
	}
	// Trims a branch node into its highest-weight leaf node.
	void trim()
	{
		if (is_leaf())
			return;
		else if (child0 && child1)
		{
			child0->trim();
			child1->trim();
			if (child0->get_weight() < child1->get_weight())
				std::swap(child0, child1);
			set_weight(child0->get_weight());
			set_value(child0->get_value());
			child0->prune(true);
			child1->prune(true);
			child0 = child1 = 0;
			return;
		}
		else if (child1)
			std::swap(child0, child1);

		child0->trim();
		set_weight(child0->get_weight());
		set_value(child0->get_value());
		child0->prune(true);
		child0 = 0;
	}
	// Optimizes the tree for Nemesis encoding.
	void optimize_internal(size_t code, unsigned char nbits)
	{
		if ((nbits == 6 && code == 0x3F) || nbits >= 9)
		{	
			// This stops the recursion as we reach the %111111 pattern, which
			// indicates inline RLE in Nemesis format, or have exceeded the
			// allotted number of bits per nibble.
			prune(false);
			return;
		}
		else if (is_leaf())
			return;
		else
		{
			code <<= 1;
			nbits++;
			// We do not want codes longer than 8 bits.
			if (nbits == 8)
			{
				if (child0 && child1)
				{
					child0->trim();
					child1->trim();
					if (child1->get_weight() > child0->get_weight())
						std::swap(child0, child1);
				}
				else if (child1)
				{
					std::swap(child0, child1);
					child0->trim();
				}
			}
			// And neither do we want codes with bit pattern %111111 as prefix.
			else if (nbits == 6 && code == 0x3E)
			{
				// We only want the child ending in 0.
				// Prune the lowest-weight of the child nodes.
				if (child0 && child1)
				{
					if (child1->get_weight() > child0->get_weight())
						std::swap(child0, child1);
					child1->prune(true);
					child1 = 0;
				}
				else if (child1)
					std::swap(child0, child1);

				child0->optimize_internal(code, nbits);
			}
			else
			{
				if (child0 && !child0->is_leaf())
					child0->optimize_internal(code, nbits);
				if (child1 && !child1->is_leaf())
					child1->optimize_internal(code|1, nbits);
				if (child0 && child1)
				{
					if (child1->is_leaf() && (!child0->is_leaf() ||
					                          child0->weight < child1->weight))
						std::swap(child0, child1);
					size_t bitmask = (size_t(1) << nbits) - size_t(1);
					if (child1->is_leaf() && (code|1) == bitmask)
						child1 = new node(child1, 0);
				}
				else if (child1 && child1->is_leaf())
					std::swap(child0, child1);
			}
		}
	}
	unsigned char max_code_len_internal(unsigned char len) const
	{
		if (is_leaf())
			return len;
		unsigned char c0 = 0, c1 = 0;
		if (child0)
			c0 = child0->max_code_len_internal(len + 1);
		if (child1)
			c1 = child1->max_code_len_internal(len + 1);
		return std::max(c0,c1);
	}
public:
	// Construct a new leaf node for character c.
	node(nibble_run const& val, int wgt = -1)
	: value (val), weight(wgt), child0(0), child1(0)
	{      }
	// Construct a new internal node that has children c1 and c2.
	node(node *c0, node *c1)
	{
		value = nibble_run();
		weight = (c0 ? c0->weight : 0) + (c1 ? c1->weight : 0);
		node const *temp = c0;
		while (temp && !temp->is_leaf())
			temp = temp->child1;
		if (temp && temp->value.get_nibble() == 0xff)
		{
			child0 = c1;
			child1 = c0;
			return;
		}
		temp = c1;
		while (temp && !temp->is_leaf())
			temp = temp->child1;
		if (temp && temp->value.get_nibble() == 0xff)
		{
			child0 = c0;
			child1 = c1;
			return;
		}
		if (c1 && c1->is_leaf() && (!c0 || !c0->is_leaf() || c0->weight < c1->weight))
		{
			child0 = c1;
			child1 = c0;
		}
		else
		{
			child0 = c0;
			child1 = c1;
		}
	}
	// Free the memory used by the child nodes.
	void prune(bool del)
	{
		if (child0)
			child0->prune(true);
		if (child1)
			child1->prune(true);
		child0 = child1 = 0;
		if (del)
			delete this;
	}
	// Comparison operators.
	bool operator<(node const& other) const
	{	return weight < other.weight;	}
	bool operator>(node const& other) const
	{	return weight > other.weight;	}
	// This tells if the node is a leaf or a branch.
	bool is_leaf() const
	{   return child0 == 0 && child1 == 0;  }
	// Getters/setters for all properties.
	node const *get_child0() const
	{	return child0;	}
	node const *get_child1() const
	{	return child1;	}
	int get_weight() const
	{	return weight;	}
	nibble_run const& get_value() const
	{	return value;	}
	void set_child0(node *c0)
	{	child0 = c0;	}
	void set_child1(node *c1)
	{	child1 = c1;	}
	void set_weight(int w)
	{	weight = w;	}
	void set_value(nibble_run const& v)
	{	value = v;	}
	// This goes through the tree, starting with the current node, generating
	// a map associating a nibble run with its code.
	void traverse(std::map<nibble_run, std::pair<size_t, unsigned char> >& codemap) const
	{	traverse_internal(0, 0, codemap);	}
	// Optimizes the tree for Nemesis encoding.
	void optimize()
	{	optimize_internal(0, 0);	}
	unsigned char max_code_len() const
	{	return max_code_len_internal(0);	}
	node const *node_for_code(size_t code, unsigned char nbits) const
	{
		if (nbits == 0)
			return this;
		nbits--;
		if (((code >> nbits) & 1) != 0)
			return child1 ? child1->node_for_code(code, nbits) : 0;
		else
			return child0 ? child0->node_for_code(code, nbits) : 0;
	}
};

void nemesis::decode_header(std::istream& Src, std::ostream& Dst,
                            std::map<unsigned char, nibble_run>& codemap)
{
	// storage for output value to decompression buffer
	size_t out_val = 0;

	// main loop. Header is terminated by the value of 0xFF
	for (size_t in_val = Read1(Src); in_val != 0xFF; in_val = Read1(Src))
	{
		// if most significant bit is set, store the last 4 bits and discard the rest
		if ((in_val & 0x80) != 0)
		{
			out_val = in_val & 0xf;
			in_val = Read1(Src);
		}

		nibble_run run(out_val, ((in_val & 0x70) >> 4) + 1);

		// Read the run's code from stream.
		codemap[Read1(Src)] = run;
	}
}

void nemesis::decode_internal(std::istream& Src, std::ostream& Dst,
                              std::map<unsigned char, nibble_run>& codemap,
                              size_t rtiles, bool alt_out)
{
	// This buffer is used for alternating mode decoding.
	std::stringstream dst(std::ios::in|std::ios::out|std::ios::binary);

	// Set bit I/O streams.
	ibitstream<unsigned char> bits(Src);
	obitstream<unsigned char> out(alt_out ? dst : Dst);
	unsigned char code = bits.get();

	// When to stop decoding: number of tiles * $20 bytes per tile * 8 bits per byte.
	size_t total_bits = rtiles << 8, bits_written = 0;
	while (bits_written < total_bits)
	{
		if (code == 0x3f)
		{
			// Bit pattern %111111; inline RLE.
			// First 3 bits are repetition count, followed by the inlined nibble.
			size_t cnt    = bits.read(3) + 1, nibble = bits.read(4);
			bits_written += cnt * 4;

			// Write single nibble if needed.
			if ((cnt & 1) != 0)
				out.write(nibble, 4);

			// Now write pairs of nibbles.
			cnt >>= 1;
			nibble |= (nibble << 4);
			for (size_t i = 0; i < cnt; i++)
				out.write(nibble, 8);

			// Read next bit, replacing previous data.
			code = bits.get();
		}
		else
		{
			// Find out if the data so far is a nibble code.
			std::map<unsigned char, nibble_run>::const_iterator it = codemap.find(code);
			if (it != codemap.end())
			{
				// If it is, then it is time to output the encoded nibble run.
				nibble_run const& run = it->second;
				size_t nibble = run.get_nibble();
				size_t cnt    = run.get_count();
				bits_written += cnt * 4;

				// Write single nibble if needed.
				if ((cnt & 1) != 0)
					out.write(nibble, 4);

				// Now write pairs of nibbles.
				cnt >>= 1;
				nibble |= (nibble << 4);
				for (size_t i = 0; i < cnt; i++)
					out.write(nibble, 8);

				// Read next bit, replacing previous data.
				code = bits.get();
			}
			else
				// Read next bit and append to current data.
				code = (code << 1) | bits.get();
		}
	}

	// Write out any remaining bits, padding with zeroes.
	out.flush();

	if (alt_out)
	{
		// For alternating decoding, we must now incrementally XOR and output
		// the lines.
		dst.seekg(0, std::ios::end);
		std::streampos sz = dst.tellg();
		dst.seekg(0);
		dst.clear();
		unsigned long in = LittleEndian::Read4(dst);
		LittleEndian::Write4(Dst, in);
		while (dst.tellg() < sz)
		{
			in ^= LittleEndian::Read4(dst);
			LittleEndian::Write4(Dst, in);
		}
	}
}

bool nemesis::decode(std::istream& Src, std::ostream& Dst, std::streampos Location)
{
	Src.seekg(Location);

	std::map<unsigned char, nibble_run> codemap;
	size_t rtiles = BigEndian::Read2(Src);
	// sets the output mode based on the value of the first bit
	bool alt_out = (rtiles & 0x8000) != 0;
	rtiles &= 0x7fff;

	decode_header(Src, Dst, codemap);
	decode_internal(Src, Dst, codemap, rtiles, alt_out);
	return true;
}

void nemesis::encode_internal(std::istream& Src, std::ostream& Dst, int mode, size_t sz)
{
	// Unpack source so we don't have to deal with nibble IO after.
	std::vector<unsigned char> unpack;
	for (size_t i = 0; i < sz; i++)
	{
		size_t c = Read1(Src);
		unpack.push_back((c & 0xf0) >> 4);
		unpack.push_back((c & 0x0f));
	}
	unpack.push_back(0xff);

	// Build RLE nibble runs, RLE-encoding the nibble runs as we go along.
	// Maximum run length is 8, meaning 7 repetitions.
	std::vector<nibble_run> rleSrc;
	std::map<nibble_run,size_t> counts;
	nibble_run curr(unpack[0], 0);
	for (size_t i = 1; i < unpack.size(); i++)
	{
		nibble_run next(unpack[i], 0);
		if (next.get_nibble() != curr.get_nibble() || curr.get_count() >= 7)
		{
			rleSrc.push_back(curr);
			counts[curr] += 1;
			curr = next;
		}
		else
			curr.set_count(curr.get_count()+1);
	}
	// No longer needed.
	unpack.clear();

	// Build priority map.
	std::priority_queue<node, std::vector<node>, std::greater<node> > q;
	for (std::map<nibble_run,size_t>::iterator it = counts.begin();
		 it != counts.end(); ++it)
		// No point in including anything with weight less than 2, as they
		// would actually increase compressed file size if it were used.
		if (it->second > 1)
			q.push(node(it->first, it->second));

	// No longer needed.
	counts.clear();

	node const *invnode = 0;
	int wgt = -1;
	for (size_t i = 0; i < 100; i++)
	{
		std::priority_queue<node, std::vector<node>, std::greater<node> > q0 = q;

		if (invnode)
			q0.push(node(nibble_run(0xff,0), wgt = invnode->get_weight()));
		while (q0.size() > 1)
		{
			node *child1 = new node(q0.top());
			q0.pop();
			node *child0 = new node(q0.top());
			q0.pop();
			q0.push(node(child0, child1));
		}
		node tree0 = q0.top();
		q0.pop();

		node const *newinvnode = tree0.node_for_code(0x3f, 6);
		if (!newinvnode || newinvnode->get_value().get_nibble() == 0xff)
			break;
		invnode = newinvnode;
	}

	if (wgt >= 0)
		q.push(node(nibble_run(0xff,0), wgt));

	// This loop removes the two smallest nodes from the
	// queue.  It creates a new internal node that has
	// those two nodes as children. The new internal node
	// is then inserted into the priority queue.  When there
	// is only one node in the priority queue, the tree
	// is complete.
	while (q.size() > 1)
	{
		node *child1 = new node(q.top());
		q.pop();
		node *child0 = new node(q.top());
		q.pop();
		q.push(node(child0, child1));
	}

	// The first phase of the Huffman encoding is now done: we have a binary
	// tree from which we can construct the prefix-free bit codes for the
	// nibble runs in the file.
	node tree = q.top();
	q.pop();

	// Optimize for Nemesis encoding: all codes end in 0, bit pattern %111111
	// is forbidden (as are codes starting with it), maximum code length is
	// 8 bits.
	tree.optimize();

	// Time now to walk through the Huffman tree and build the code map.
	std::map<nibble_run, std::pair<size_t, unsigned char> > codemap;
	tree.traverse(codemap);
	tree.prune(false);

	// We now have a prefix-free code map associating the RLE-encoded nibble
	// runs with their code. Now we write the file.
	// Write header.
	BigEndian::Write2(Dst, (mode << 15) | (sz >> 5));
	unsigned char lastnibble = 0xff;
	for (std::map<nibble_run, std::pair<size_t, unsigned char> >::iterator it = codemap.begin();
	     it != codemap.end(); ++it)
	{
		nibble_run const& run = it->first;
		if (run.get_nibble() != lastnibble)
		{
			// 0x80 marks byte as setting a new nibble.
			Write1(Dst, 0x80 | run.get_nibble());
			lastnibble = run.get_nibble();
		}
		size_t code = (it->second).first, len = (it->second).second;
		Write1(Dst, (run.get_count() << 4) | (len));
		Write1(Dst, code);
	}

	// Mark end of header.
	Write1(Dst, 0xff);

	// Time to write the encoded bitstream.
	obitstream<unsigned char> bits(Dst);

	// The RLE-encoded source makes for a far faster encode as we simply
	// use the nibble runs as an index into the map, meaning a quick binary
	// search gives us the code to use (if in the map) or tells us that we
	// need to use inline RLE.
	for (std::vector<nibble_run>::iterator it = rleSrc.begin();
	     it != rleSrc.end(); ++it)
	{
		nibble_run const& run = *it;
		std::map<nibble_run, std::pair<size_t, unsigned char> >::iterator val =
			codemap.find(run);
		if (val != codemap.end())
		{
			unsigned char code = (val->second).first, len = (val->second).second;
			bits.write(code, len);
		}
		else
		{
			bits.write(0x3f, 6);
			bits.write(run.get_count(), 3);
			bits.write(run.get_nibble(), 4);
		}
	}
	// Fill remainder of last byte with zeroes and write if needed.
	bits.flush();
}

bool nemesis::encode(std::istream& Src, std::ostream& Dst)
{
	// We will use these as output buffers, as well as an input/output
	// buffers for the padded Nemesis input.
	std::stringstream mode0buf(std::ios::in|std::ios::out|std::ios::binary),
	                  mode1buf(std::ios::in|std::ios::out|std::ios::binary),
	                  src(std::ios::in|std::ios::out|std::ios::binary);

	// Get original source length.
	Src.seekg(0, std::ios::end);
	std::streampos sz = Src.tellg();
	Src.seekg(0);

	// Copy to buffer.
	while (Src.tellg() < sz)
		Write1(src, Read1(Src));

	// Is the source length a multiple of 32 bits?
	if ((sz & 0x1f) != 0)
	{
		// If not, pad it with zeroes until it is.
		while ((src.tellp() & 0x1f) != 0)
		{
			Write1(src, 0);
			sz += 1;
		}
	}

	// Now we will build the alternating bit stream for mode 1 compression.
	src.clear();
	src.seekg(0);

	std::string sin = src.str();
	for (size_t i = sin.size() - 4; i > 0; i -=4)
	{
		sin[i + 0] ^= sin[i - 4];
		sin[i + 1] ^= sin[i - 3];
		sin[i + 2] ^= sin[i - 2];
		sin[i + 3] ^= sin[i - 1];
	}
	std::stringstream alt(sin, std::ios::in|std::ios::out|std::ios::binary);

	// Reposition input streams to the beginning.
	src.clear();
	alt.clear();
	src.seekg(0);
	alt.seekg(0);

	// Encode in both modes.
	encode_internal(src, mode0buf, 0, sz);
	encode_internal(alt, mode1buf, 1, sz);
	
	// Reposition output streams to the start.
	mode0buf.seekg(0);
	mode1buf.seekg(0);

	// We will pick the smallest resulting stream as output.
	size_t sz0 = mode0buf.str().size(), sz1 = mode1buf.str().size();
	if (sz0 <= sz1)
		Dst.write(mode0buf.str().c_str(), mode0buf.str().size());
	else
		Dst.write(mode1buf.str().c_str(), mode1buf.str().size());

	// Pad to even size.
	if ((Dst.tellp() & 1) != 0)
		Dst.put(0);
	
	return true;
}
