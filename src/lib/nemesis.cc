/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * Copyright (C) Flamewing 2011-2016 <flamewing.sonic@gmail.com>
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

#include <mdcomp/bigendian_io.hh>
#include <mdcomp/bitstream.hh>
#include <mdcomp/ignore_unused_variable_warning.hh>
#include <mdcomp/nemesis.hh>

#include <algorithm>
#include <cstdint>
#include <istream>
#include <map>
#include <memory>
#include <ostream>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>


using std::enable_shared_from_this;
using std::fill_n;
using std::ios;
using std::iostream;
using std::istream;
using std::make_shared;
using std::map;
using std::multiset;
using std::numeric_limits;
using std::ostream;
using std::ostreambuf_iterator;
using std::priority_queue;
using std::shared_ptr;
using std::streamsize;
using std::string;
using std::stringstream;
using std::vector;

// This represents a nibble run of up to 7 repetitions of the starting nibble.
class nibble_run {
private:
    uint8_t nibble{0};    // Nibble we are interested in.
    uint8_t count{0};     // How many times the nibble is repeated.

public:
    // Constructors.
    nibble_run() noexcept = default;
    nibble_run(uint8_t n, uint8_t c) noexcept : nibble(n), count(c) {}
    nibble_run(nibble_run const& other) noexcept = default;
    nibble_run(nibble_run&& other) noexcept      = default;
    nibble_run& operator=(nibble_run const& other) noexcept = default;
    nibble_run& operator=(nibble_run&& other) noexcept = default;
    ~nibble_run() noexcept                             = default;
    // Sorting operator.
    bool operator<(nibble_run const& other) const noexcept {
        return (nibble < other.nibble)
               || (nibble == other.nibble && count < other.count);
    }
    // Sorting operator.
    bool operator>(nibble_run const& other) const noexcept {
        return other < *this;
    }
    bool operator==(nibble_run const& other) const noexcept {
        return !(*this < other) && !(other < *this);
    }
    bool operator!=(nibble_run const& other) const noexcept {
        return !(*this == other);
    }
    // Getters/setters for all properties.
    uint8_t get_nibble() const noexcept {
        return nibble;
    }
    uint8_t get_count() const noexcept {
        return count;
    }
    void set_nibble(uint8_t const tf) noexcept {
        nibble = tf;
    }
    void set_count(uint8_t const tf) noexcept {
        count = tf;
    }
};

struct SizeFreqNibble {
    size_t     count{0};
    nibble_run nibble;
    uint8_t    codelen{0};
    SizeFreqNibble(
            size_t cnt, nibble_run const& nib, uint8_t const len) noexcept
            : count(cnt), nibble(nib), codelen(len) {}
    SizeFreqNibble() noexcept                            = default;
    SizeFreqNibble(SizeFreqNibble const& other) noexcept = default;
    SizeFreqNibble(SizeFreqNibble&& other) noexcept      = default;
    SizeFreqNibble& operator=(SizeFreqNibble const& other) noexcept = default;
    SizeFreqNibble& operator=(SizeFreqNibble&& other) noexcept = default;
    ~SizeFreqNibble() noexcept                                 = default;
};

struct Code {
    size_t  code;
    uint8_t len;
    bool    operator<(Code const& rhs) const noexcept {
        return code < rhs.code || (code == rhs.code && len < rhs.len);
    }
    Code(size_t const c, uint8_t const l) noexcept : code(c), len(l) {}
    Code() noexcept                  = default;
    Code(Code const& other) noexcept = default;
    Code(Code&& other) noexcept      = default;
    Code& operator=(Code const& other) noexcept = default;
    Code& operator=(Code&& other) noexcept = default;
    ~Code() noexcept                       = default;
};

using CodeSizeMap   = map<nibble_run, uint8_t>;
using RunCountMap   = map<nibble_run, size_t>;
using NibbleCodeMap = map<nibble_run, Code>;
using CodeNibbleMap = map<Code, nibble_run>;

// Slightly based on code by Mark Nelson for Huffman encoding.
// http://marknelson.us/1996/01/01/priority-queues/
// This represents a node (leaf or branch) in the Huffman encoding tree.
class node : public enable_shared_from_this<node> {
private:
    shared_ptr<node> child0, child1;
    size_t           weight;
    nibble_run       value;

public:
    // Construct a new leaf node for character c.
    node(nibble_run const& val, size_t const wgt) noexcept
            : weight(wgt), value(val) {}
    // Construct a new internal node that has children c1 and c2.
    node(const shared_ptr<node>& c0, const shared_ptr<node>& c1) noexcept {
        value  = nibble_run{0, 0};
        weight = c0->weight + c1->weight;
        child0 = c0;
        child1 = c1;
    }
    node(node const& other) noexcept = default;
    node(node&& other) noexcept      = default;
    node& operator=(node const& other) noexcept = default;
    node& operator=(node&& other) noexcept = default;
    ~node() noexcept {
        child0.reset();
        child1.reset();
    }
    // Free the memory used by the child nodes.
    void prune() noexcept {
        child0.reset();
        child1.reset();
    }
    // Comparison operators.
    bool operator<(node const& other) const noexcept {
        return weight < other.weight;
    }
    bool operator>(node const& other) const noexcept {
        return other < *this;
    }
    // This tells if the node is a leaf or a branch.
    bool is_leaf() const noexcept {
        return child0 == nullptr && child1 == nullptr;
    }
    // Getters/setters for all properties.
    shared_ptr<node const> get_child0() const noexcept {
        return child0;
    }
    shared_ptr<node const> get_child1() const noexcept {
        return child1;
    }
    size_t get_weight() const noexcept {
        return weight;
    }
    nibble_run const& get_value() const noexcept {
        return value;
    }
    void set_child0(shared_ptr<node> c0) noexcept {
        child0 = std::move(c0);
    }
    void set_child1(shared_ptr<node> c1) noexcept {
        child1 = std::move(c1);
    }
    void set_weight(size_t w) noexcept {
        weight = w;
    }
    void set_value(nibble_run const& v) noexcept {
        value = v;
    }
    // This goes through the tree, starting with the current node, generating
    // a map associating a nibble run with its code length.
    void traverse(CodeSizeMap& sizemap) const noexcept {
        if (is_leaf()) {
            sizemap[value] += 1;
        } else {
            if (child0) {
                child0->traverse(sizemap);
            }
            if (child1) {
                child1->traverse(sizemap);
            }
        }
    }
};

using NodeVector = vector<shared_ptr<node>>;

struct Compare_size {
    bool operator()(SizeFreqNibble const& lhs, SizeFreqNibble const& rhs)
            const noexcept {
        if (lhs.codelen < rhs.codelen) {
            return true;
        }
        if (lhs.codelen > rhs.codelen) {
            return false;
        }
        // rhs.codelen == lhs.codelen
        if (lhs.count > rhs.count) {
            return true;
        }
        if (lhs.count < rhs.count) {
            return false;
        }
        // rhs.count == lhs.count
        nibble_run const& left  = lhs.nibble;
        nibble_run const  right = rhs.nibble;
        return (left.get_nibble() < right.get_nibble()
                || (left.get_nibble() == right.get_nibble()
                    && left.get_count() > right.get_count()));
    }
};

struct Compare_node {
    bool operator()(shared_ptr<node> const& lhs, shared_ptr<node> const& rhs)
            const noexcept {
#if 1
        if (*lhs > *rhs) {
            return true;
        }
        if (*lhs < *rhs) {
            return false;
        }
        return lhs->get_value().get_count() < rhs->get_value().get_count();
#else
        return *lhs > *rhs;
#endif
    }
    // Just discard the lowest weighted item.
    void update(NodeVector& qt, NibbleCodeMap& codes) const noexcept {
        ignore_unused_variable_warning(codes);
        pop_heap(qt.begin(), qt.end(), *this);
        qt.pop_back();
    }
};

struct Compare_node2 {
    static NibbleCodeMap codemap;
    bool operator()(shared_ptr<node> const& lhs, shared_ptr<node> const& rhs)
            const noexcept {
        if (codemap.empty()) {
            if (*lhs < *rhs) {
                return true;
            }
            if (*lhs > *rhs) {
                return false;
            }
            return lhs->get_value().get_count() > rhs->get_value().get_count();
        }
        nibble_run const lnib = lhs->get_value();
        nibble_run const rnib = rhs->get_value();

        size_t     lclen;
        size_t     rclen;
        auto const lit = codemap.find(lnib);
        auto const rit = codemap.find(rnib);
        if (lit == codemap.end()) {
            lclen = (6 + 7) * lhs->get_weight();
        } else {
            size_t bitcnt = (lit->second).len;
            lclen         = (bitcnt & 0x7f) * lhs->get_weight() + 16;
        }
        if (rit == codemap.end()) {
            rclen = (6 + 7) * rhs->get_weight();
        } else {
            size_t bitcnt = (rit->second).len;
            rclen         = (bitcnt & 0x7f) * rhs->get_weight() + 16;
        }
        if (lclen > rclen) {
            return true;
        }
        if (lclen < rclen) {
            return false;
        }

        size_t lblen;
        size_t rblen;
        lblen = (lnib.get_count() + 1) * lhs->get_weight();
        rblen = (rnib.get_count() + 1) * rhs->get_weight();
        if (lblen < rblen) {
            return true;
        }
        if (lblen > rblen) {
            return false;
        }
        return lnib.get_count() < rnib.get_count();
    }
    // Resort the heap using weights from the previous iteration, then discards
    // the lowest weighted item.
    void update(NodeVector& qt, NibbleCodeMap& codes) const noexcept {
        codemap = codes;
        make_heap(qt.begin(), qt.end(), *this);
        pop_heap(qt.begin(), qt.end(), *this);
        qt.pop_back();
    }
};

NibbleCodeMap Compare_node2::codemap;

template <>
size_t moduled_nemesis::PadMaskBits = 1U;

class nemesis_internal {
public:
    static void decode_header(std::istream& Src, CodeNibbleMap& codemap) {
        // storage for output value to decompression buffer
        size_t out_val = 0;

        // main loop. Header is terminated by the value of 0xFF
        for (size_t in_val = Read1(Src); in_val != 0xFF; in_val = Read1(Src)) {
            // if most significant bit is set, store the last 4 bits and discard
            // the rest
            if ((in_val & 0x80) != 0) {
                out_val = in_val & 0xf;
                in_val  = Read1(Src);
            }

            nibble_run const run(out_val, ((in_val & 0x70) >> 4) + 1);

            size_t const  code = Read1(Src);
            uint8_t const len  = in_val & 0xf;
            // Read the run's code from stream.
            codemap[Code{code, len}] = run;
        }
    }

    static void
            decode(std::istream& Src, std::ostream& Dst, CodeNibbleMap& codemap,
                   size_t const rtiles, bool const alt_out = false) {
        // This buffer is used for alternating mode decoding.
        stringstream dst(ios::in | ios::out | ios::binary);

        // Set bit I/O streams.
        ibitstream<uint8_t, true> bits(Src);
        obitstream<uint8_t>       out(dst);
        size_t                    code = bits.pop();
        uint8_t                   len  = 1;

        // When to stop decoding: number of tiles * $20 bytes per tile * 8 bits
        // per byte.
        size_t total_bits   = rtiles << 8;
        size_t bits_written = 0;
        while (bits_written < total_bits) {
            if (code == 0x3f && len == 6) {
                // Bit pattern %111111; inline RLE.
                // First 3 bits are repetition count, followed by the inlined
                // nibble.
                size_t cnt    = bits.read(3) + 1;
                size_t nibble = bits.read(4);
                bits_written += cnt * 4;

                // Write single nibble if needed.
                if ((cnt & 1) != 0) {
                    out.write(nibble, 4);
                }

                // Now write pairs of nibbles.
                cnt >>= 1;
                nibble |= (nibble << 4);
                for (size_t i = 0; i < cnt; i++) {
                    out.write(nibble, 8);
                }

                if (bits_written >= total_bits) {
                    break;
                }

                // Read next bit, replacing previous data.
                code = bits.pop();
                len  = 1;
            } else {
                // Find out if the data so far is a nibble code.
                auto const it = codemap.find(Code{code, len});
                if (it != codemap.end()) {
                    // If it is, then it is time to output the encoded nibble
                    // run.
                    nibble_run const& run    = it->second;
                    size_t            nibble = run.get_nibble();
                    size_t            cnt    = run.get_count();
                    bits_written += cnt * 4;

                    // Write single nibble if needed.
                    if ((cnt & 1) != 0) {
                        out.write(nibble, 4);
                    }

                    // Now write pairs of nibbles.
                    cnt >>= 1;
                    nibble |= (nibble << 4);
                    for (size_t i = 0; i < cnt; i++) {
                        out.write(nibble, 8);
                    }

                    if (bits_written >= total_bits) {
                        break;
                    }

                    // Read next bit, replacing previous data.
                    code = bits.pop();
                    len  = 1;
                } else {
                    // Read next bit and append to current data.
                    code = (code << 1) | bits.pop();
                    len++;
                }
            }
        }

        // Write out any remaining bits, padding with zeroes.
        out.flush();

        if (alt_out) {
            // For alternating decoding, we must now incrementally XOR and
            // output the lines.
            dst.seekg(0);
            dst.clear();
            uint32_t in = LittleEndian::Read4(dst);
            LittleEndian::Write4(Dst, in);
            while (size_t(dst.tellg()) < rtiles << 5) {
                in ^= LittleEndian::Read4(dst);
                LittleEndian::Write4(Dst, in);
            }
        } else {
            Dst.write(dst.str().c_str(), rtiles << 5);
        }
    }

    static size_t estimate_file_size(
            NibbleCodeMap& tempcodemap, RunCountMap& counts) {
        // We now compute the final file size for this code table.
        // 2 bytes at the start of the file, plus 1 byte at the end of the
        // code table.
        size_t tempsize_est = 3 * 8;
        size_t last         = 0xff;
        // Start with any nibble runs with their own code.
        for (auto& elem : tempcodemap) {
            // Each new nibble needs an extra byte.
            if (last != elem.first.get_nibble()) {
                tempsize_est += 8;
                // Be sure to SET the last nibble to the current nibble... this
                // fixes a bug that caused file sizes to increase in some cases.
                last = elem.first.get_nibble();
            }
            // 2 bytes per nibble run in the table.
            tempsize_est += 2 * 8;
            // How many bits this nibble run uses in the file.
            tempsize_est += counts[elem.first] * (elem.second).len;
        }

        // Supplementary code map for the nibble runs that can be broken up into
        // shorter nibble runs with a smaller bit length than inlining.
        NibbleCodeMap supcodemap;
        // Now we will compute the size requirements for inline nibble runs.
        for (auto& count : counts) {
            // Find out if this nibble run has a code for it.
            auto it2 = tempcodemap.find(count.first);
            if (it2 == tempcodemap.end()) {
                // Nibble run does not have its own code. We need to find out if
                // we can break it up into smaller nibble runs with total code
                // size less than 13 bits or if we need to inline it (13 bits).
                if (count.first.get_count() == 0) {
                    // If this is a nibble run with zero repeats, we can't break
                    // it up into smaller runs, so we inline it.
                    tempsize_est += (6 + 7) * count.second;
                } else if (count.first.get_count() == 1) {
                    // We stand a chance of breaking the nibble run.

                    // This case is rather trivial, so we hard-code it.
                    // We can break this up only as 2 consecutive runs of a
                    // nibble run with count == 0.
                    nibble_run trg{count.first.get_nibble(), 0};
                    it2 = tempcodemap.find(trg);
                    if (it2 == tempcodemap.end() || (it2->second).len > 6) {
                        // The smaller nibble run either does not have its own
                        // code or it results in a longer bit code when doubled
                        // up than would result from inlining the run. In either
                        // case, we inline the nibble run.
                        tempsize_est += (6 + 7) * count.second;
                    } else {
                        // The smaller nibble run has a small enough code that
                        // it is more efficient to use it twice than to inline
                        // our nibble run. So we do exactly that, by adding a
                        // (temporary) entry in the supplementary codemap, which
                        // will later be merged into the main codemap.
                        size_t  code = (it2->second).code;
                        uint8_t len  = (it2->second).len;
                        code         = (code << len) | code;
                        len <<= 1;
                        tempsize_est += len * count.second;
                        len |= 0x80;    // Flag this as a false code.
                        supcodemap[count.first] = Code{code, len};
                    }
                } else {
                    // We stand a chance of breaking it the nibble run.

                    // This is a linear optimization problem subjected to 2
                    // constraints. If the number of repeats of the current
                    // nibble run is N, then we have N dimensions. Pointer to
                    // table of linear coefficients. This table has N columns
                    // for each line.
                    size_t const* linear_coeffs;
                    // Here are some hard-coded tables, obtained by brute-force:
                    constexpr static size_t const linear_coeffs2[2][2]
                            = {{3, 0}, {1, 1}};
                    constexpr static size_t const linear_coeffs3[4][3]
                            = {{4, 0, 0}, {2, 1, 0}, {1, 0, 1}, {0, 2, 0}};
                    constexpr static size_t const linear_coeffs4[6][4]
                            = {{5, 0, 0, 0}, {3, 1, 0, 0}, {2, 0, 1, 0},
                               {1, 2, 0, 0}, {1, 0, 0, 1}, {0, 1, 1, 0}};
                    constexpr static size_t const linear_coeffs5[10][5] = {
                            {6, 0, 0, 0, 0}, {4, 1, 0, 0, 0}, {3, 0, 1, 0, 0},
                            {2, 2, 0, 0, 0}, {2, 0, 0, 1, 0}, {1, 1, 1, 0, 0},
                            {1, 0, 0, 0, 1}, {0, 3, 0, 0, 0}, {0, 1, 0, 1, 0},
                            {0, 0, 2, 0, 0}};
                    constexpr static size_t const linear_coeffs6[14][6]
                            = {{7, 0, 0, 0, 0, 0}, {5, 1, 0, 0, 0, 0},
                               {4, 0, 1, 0, 0, 0}, {3, 2, 0, 0, 0, 0},
                               {3, 0, 0, 1, 0, 0}, {2, 1, 1, 0, 0, 0},
                               {2, 0, 0, 0, 1, 0}, {1, 3, 0, 0, 0, 0},
                               {1, 1, 0, 1, 0, 0}, {1, 0, 2, 0, 0, 0},
                               {1, 0, 0, 0, 0, 1}, {0, 2, 1, 0, 0, 0},
                               {0, 1, 0, 0, 1, 0}, {0, 0, 1, 1, 0, 0}};
                    constexpr static size_t const linear_coeffs7[21][7]
                            = {{8, 0, 0, 0, 0, 0, 0}, {6, 1, 0, 0, 0, 0, 0},
                               {5, 0, 1, 0, 0, 0, 0}, {4, 2, 0, 0, 0, 0, 0},
                               {4, 0, 0, 1, 0, 0, 0}, {3, 1, 1, 0, 0, 0, 0},
                               {3, 0, 0, 0, 1, 0, 0}, {2, 3, 0, 0, 0, 0, 0},
                               {2, 1, 0, 1, 0, 0, 0}, {2, 0, 2, 0, 0, 0, 0},
                               {2, 0, 0, 0, 0, 1, 0}, {1, 2, 1, 0, 0, 0, 0},
                               {1, 1, 0, 0, 1, 0, 0}, {1, 0, 1, 1, 0, 0, 0},
                               {1, 0, 0, 0, 0, 0, 1}, {0, 4, 0, 0, 0, 0, 0},
                               {0, 2, 0, 1, 0, 0, 0}, {0, 1, 2, 0, 0, 0, 0},
                               {0, 1, 0, 0, 0, 1, 0}, {0, 0, 1, 0, 1, 0, 0},
                               {0, 0, 0, 2, 0, 0, 0}};
                    size_t const n = count.first.get_count();
                    size_t       rows;
                    // Get correct coefficient table:
                    switch (n) {
                    case 2:
                        linear_coeffs = &linear_coeffs2[0][0];
                        rows          = 2;
                        break;
                    case 3:
                        linear_coeffs = &linear_coeffs3[0][0];
                        rows          = 4;
                        break;
                    case 4:
                        linear_coeffs = &linear_coeffs4[0][0];
                        rows          = 6;
                        break;
                    case 5:
                        linear_coeffs = &linear_coeffs5[0][0];
                        rows          = 10;
                        break;
                    case 6:
                        linear_coeffs = &linear_coeffs6[0][0];
                        rows          = 14;
                        break;
                    case 7:
                    default:
                        linear_coeffs = &linear_coeffs7[0][0];
                        rows          = 21;
                        break;
                    }

                    uint8_t const nibble = count.first.get_nibble();
                    // Vector containing the code length of each nibble run, or
                    // 13 if the nibble run is not in the codemap.
                    vector<size_t> runlen;
                    // Init vector.
                    for (size_t i = 0; i < n; i++) {
                        // Is this run in the codemap?
                        nibble_run trg(nibble, i);
                        auto       it3 = tempcodemap.find(trg);
                        if (it3 == tempcodemap.end()) {
                            // It is not.
                            // Put inline length in the vector.
                            runlen.push_back(6 + 7);
                        } else {
                            // It is.
                            // Put code length in the vector.
                            runlen.push_back((it3->second).len);
                        }
                    }

                    // Now go through the linear coefficient table and tally up
                    // the total code size, looking for the best case.
                    // The best size is initialized to be the inlined case.
                    size_t best_size = 6 + 7;
                    int    best_line = -1;
                    size_t base      = 0;
                    for (size_t i = 0; i < rows; i++, base += n) {
                        // Tally up the code length for this coefficient line.
                        size_t len = 0;
                        for (size_t j = 0; j < n; j++) {
                            size_t const c = linear_coeffs[base + j];
                            if (c == 0U) {
                                continue;
                            }

                            len += c * runlen[j];
                        }
                        // Is the length better than the best yet?
                        if (len < best_size) {
                            // If yes, store it as the best.
                            best_size = len;
                            best_line = base;
                        }
                    }
                    // Have we found a better code than inlining?
                    if (best_line >= 0) {
                        // We have; use it. To do so, we have to build the code
                        // and add it to the supplementary code table.
                        size_t code = 0;
                        size_t len  = 0;
                        for (size_t i = 0; i < n; i++) {
                            size_t const c = linear_coeffs[best_line + i];
                            if (c == 0U) {
                                continue;
                            }
                            // Is this run in the codemap?
                            nibble_run trg(nibble, i);
                            auto       it3 = tempcodemap.find(trg);
                            if (it3 != tempcodemap.end()) {
                                // It is; it MUST be, as the other case is
                                // impossible by construction.
                                for (size_t j = 0; j < c; j++) {
                                    len += (it3->second).len;
                                    code <<= (it3->second).len;
                                    code |= (it3->second).code;
                                }
                            }
                        }
                        if (len != best_size) {
                            // ERROR! DANGER! THIS IS IMPOSSIBLE!
                            // But just in case...
                            tempsize_est += (6 + 7) * count.second;
                        } else {
                            // By construction, best_size is at most 12.
                            // Flag it as a false code.
                            uint8_t const mlen = best_size | 0x80;
                            // Add it to supplementary code map.
                            supcodemap[count.first] = Code{code, mlen};
                            tempsize_est += best_size * count.second;
                        }
                    } else {
                        // No, we will have to inline it.
                        tempsize_est += (6 + 7) * count.second;
                    }
                }
            }
        }
        tempcodemap.insert(supcodemap.begin(), supcodemap.end());

        // Round up to a full byte.
        tempsize_est = (tempsize_est + 7) & ~7;

        return tempsize_est;
    }

    template <typename Compare>
    static size_t
            encode(istream& Src, ostream& Dst, size_t mode, size_t const sz,
                   Compare const& comp) {
        // Seek to start and clear all errors.
        Src.clear();
        Src.seekg(0);
        // Unpack source so we don't have to deal with nibble IO after.
        vector<uint8_t> unpack;
        for (size_t i = 0; i < sz; i++) {
            size_t const c = Read1(Src);
            unpack.push_back((c & 0xf0) >> 4);
            unpack.push_back((c & 0x0f));
        }
        unpack.push_back(0xff);

        // Build RLE nibble runs, RLE-encoding the nibble runs as we go along.
        // Maximum run length is 8, meaning 7 repetitions.
        vector<nibble_run> rleSrc;
        RunCountMap        counts;
        nibble_run         curr{unpack[0], 0};
        for (size_t i = 1; i < unpack.size(); i++) {
            nibble_run next{unpack[i], 0};
            if (next.get_nibble() != curr.get_nibble()
                || curr.get_count() >= 7) {
                rleSrc.push_back(curr);
                counts[curr] += 1;
                curr = next;
            } else {
                curr.set_count(curr.get_count() + 1);
            }
        }
        // No longer needed.
        unpack.clear();

        Compare_node2::codemap.clear();

        // We will use the Package-merge algorithm to build the optimal
        // length-limited Huffman code for the current file. To do this, we must
        // map the current problem onto the Coin Collector's problem. Build the
        // basic coin collection.
        NodeVector qt;
        qt.reserve(counts.size());
        for (auto& count : counts) {
            // No point in including anything with weight less than 2, as they
            // would actually increase compressed file size if used.
            if (count.second > 1) {
                qt.push_back(make_shared<node>(count.first, count.second));
            }
        }
        // This may seem useless, but my tests all indicate that this reduces
        // the average file size. I haven't the foggiest idea why.
        make_heap(qt.begin(), qt.end(), comp);

        // The base coin collection for the length-limited Huffman coding has
        // one coin list per character in length of the limitation. Each coin
        // list has a constant "face value", and each coin in a list has its own
        // "numismatic value". The "face value" is unimportant in the way the
        // code is structured below; the "numismatic value" of each coin is the
        // number of times the underlying nibble run appears in the source file.

        // This will hold the Huffman code map.
        // NOTE: while the codes that will be written in the header will not be
        // longer than 8 bits, it is possible that a supplementary code map will
        // add "fake" codes that are longer than 8 bits.
        NibbleCodeMap codemap;
        // Size estimate. This is used to build the optimal compressed file.
        size_t size_est = 0xffffffff;

        // We will solve the Coin Collector's problem several times, each time
        // ignoring more of the least frequent nibble runs. This allows us to
        // find *the* lowest file size.
        while (qt.size() > 1) {
            // Make a copy of the basic coin collection.
            using CoinQueue = priority_queue<
                    shared_ptr<node>, NodeVector, Compare_node>;
            CoinQueue q0(qt.begin(), qt.end());

            // We now solve the Coin collector's problem using the Package-merge
            // algorithm. The solution goes here.
            NodeVector solution;
            // This holds the packages from the last iteration.
            CoinQueue q(q0);
            size_t    target = (q0.size() - 1) << 8;
            size_t    idx    = 0;
            while (target != 0) {
                // Gets lowest bit set in its proper place:
                size_t val = (target & -target);
                size_t r   = 1 << idx;
                // Is the current denomination equal to the least denomination?
                if (r == val) {
                    // If yes, take the least valuable node and put it into the
                    // solution.
                    solution.push_back(q.top());
                    q.pop();
                    target -= r;
                }

                // The coin collection has coins of values 1 to 8; copy from the
                // original in those cases for the next step.
                CoinQueue q1;
                if (idx < 7) {
                    q1 = q0;
                }

                // Split the current list into pairs and insert the packages
                // into the next list.
                while (q.size() > 1) {
                    shared_ptr<node> child1 = q.top();
                    q.pop();
                    shared_ptr<node> child0 = q.top();
                    q.pop();
                    q1.push(make_shared<node>(child0, child1));
                }
                idx++;
                q = q1;
            }

            // The Coin Collector's problem has been solved. Now it is time to
            // map the solution back into the length-limited Huffman coding
            // problem.

            // To do that, we iterate through the solution and count how many
            // times each nibble run has been used (remember that the coin
            // collection had multiple coins associated with each nibble run) --
            // this number is the optimal bit length for the nibble run for the
            // current coin collection.
            CodeSizeMap basesizemap;
            for (auto& elem : solution) {
                (elem)->traverse(basesizemap);
            }

            // With the length-limited Huffman coding problem solved, it is now
            // time to build the code table. As input, we have a map associating
            // a nibble run to its optimal encoded bit length. We will build the
            // codes using the canonical Huffman code.

            // To do that, we will need to know how many codes we will need for
            // any given code length. Since there are only 8 valid code lengths,
            // we only need this simple array.
            size_t sizecounts[8] = {0};
            // This set contains lots more information, and is used to associate
            // the nibble run with its optimal code. It is sorted by code size,
            // then by frequency of the nibble run, then by the nibble run.
            using SizeSet = multiset<SizeFreqNibble, Compare_size>;
            SizeSet sizemap;
            for (auto& elem : basesizemap) {
                uint8_t size  = elem.second;
                size_t  count = counts[elem.first];
                sizecounts[size - 1]++;
                sizemap.emplace(count, elem.first, size);
            }

            // We now build the canonical Huffman code table.
            // "base" is the code for the first nibble run with a given bit
            // length. "carry" is how many nibble runs were demoted to a higher
            // bit length at an earlier step. "cnt" is how many nibble runs have
            // a given bit length.
            size_t base  = 0;
            size_t carry = 0;
            size_t cnt;
            // This vector contains the codes sorted by size.
            vector<Code> codes;
            for (uint8_t i = 1; i <= 8; i++) {
                // How many nibble runs have the desired bit length.
                cnt         = sizecounts[i - 1] + carry;
                carry       = 0;
                size_t mask = (size_t(1) << i) - 1;
                size_t mask2
                        = (i > 6) ? (mask & ~((size_t(1) << (i - 6)) - 1)) : 0;
                for (size_t j = 0; j < cnt; j++) {
                    // Sequential binary numbers for codes.
                    size_t code = base + j;
                    // We do not want any codes composed solely of 1's or which
                    // start with 111111, as that sequence is reserved.
                    if ((i <= 6 && code == mask) || (i > 6 && code == mask2)) {
                        // We must demote this many nibble runs to a longer
                        // code.
                        carry = cnt - j;
                        cnt   = j;
                        break;
                    }
                    codes.emplace_back(code, i);
                }
                // This is the beginning bit pattern for the next bit length.
                base = (base + cnt) << 1;
            }

            // With the canonical table build, the codemap can finally be built.
            NibbleCodeMap tempcodemap;
            size_t        pos = 0;
            for (auto it = sizemap.begin();
                 it != sizemap.end() && pos < codes.size(); ++it, pos++) {
                tempcodemap[it->nibble] = codes[pos];
            }

            // We now compute the final file size for this code table.
            size_t tempsize_est = estimate_file_size(tempcodemap, counts);

            // This may resort the items. After that, it will discard the lowest
            // weighted item.
            comp.update(qt, tempcodemap);

            // Is this iteration better than the best?
            if (tempsize_est < size_est) {
                // If yes, save the codemap and file size.
                codemap  = tempcodemap;
                size_est = tempsize_est;
            }
        }
        // Special case.
        if (qt.size() == 1) {
            NibbleCodeMap    tempcodemap;
            shared_ptr<node> child          = qt.front();
            tempcodemap[child->get_value()] = Code{0U, 1};
            size_t const tempsize_est = estimate_file_size(tempcodemap, counts);

            // Is this iteration better than the best?
            if (tempsize_est < size_est) {
                // If yes, save the codemap and file size.
                codemap  = tempcodemap;
                size_est = tempsize_est;
            }
        }
        // This is no longer needed.
        counts.clear();

        // We now have a prefix-free code map associating the RLE-encoded nibble
        // runs with their code. Now we write the file.
        // Write header.
        BigEndian::Write2(Dst, (mode << 15) | (sz >> 5));
        uint8_t lastnibble = 0xff;
        for (auto& elem : codemap) {
            nibble_run const& run  = elem.first;
            size_t const      code = (elem.second).code;
            uint8_t           len  = (elem.second).len;
            // len with bit 7 set is a special device for further reducing file
            // size, and should NOT be on the table.
            if ((len & 0x80) != 0) {
                continue;
            }
            if (run.get_nibble() != lastnibble) {
                // 0x80 marks byte as setting a new nibble.
                Write1(Dst, 0x80 | run.get_nibble());
                lastnibble = run.get_nibble();
            }

            Write1(Dst, (run.get_count() << 4) | (len));
            Write1(Dst, code);
        }

        // Mark end of header.
        Write1(Dst, 0xff);

        // Time to write the encoded bitstream.
        obitstream<uint8_t> bits(Dst);

        // The RLE-encoded source makes for a far faster encode as we simply
        // use the nibble runs as an index into the map, meaning a quick binary
        // search gives us the code to use (if in the map) or tells us that we
        // need to use inline RLE.
        for (auto& run : rleSrc) {
            auto val = codemap.find(run);
            if (val != codemap.end()) {
                size_t const code = (val->second).code;
                uint8_t      len  = (val->second).len;
                // len with bit 7 set is a device to bypass the code table at
                // the start of the file. We need to clear the bit here before
                // writing the code to the file.
                len &= 0x7f;
                // We can have codes in the 9-12 range due to the break up of
                // large inlined runs into smaller non-inlined runs. Deal with
                // those high bits first, if needed.
                if (len > 8) {
                    bits.write(
                            static_cast<uint8_t>((code >> 8) & 0xff), len - 8);
                    len = 8;
                }
                bits.write(static_cast<uint8_t>(code & 0xff), len);
            } else {
                bits.write(0x3f, 6);
                bits.write(run.get_count(), 3);
                bits.write(run.get_nibble(), 4);
            }
        }
        // Fill remainder of last byte with zeroes and write if needed.
        bits.flush();
        return Dst.tellp();
    }
};

bool nemesis::decode(istream& Src, ostream& Dst) {
    CodeNibbleMap codemap;
    size_t        rtiles = BigEndian::Read2(Src);
    // sets the output mode based on the value of the first bit
    bool alt_out = (rtiles & 0x8000) != 0;
    rtiles &= 0x7fff;

    if (rtiles > 0) {
        nemesis_internal::decode_header(Src, codemap);
        nemesis_internal::decode(Src, Dst, codemap, rtiles, alt_out);
    }
    return true;
}

bool nemesis::encode(istream& Src, ostream& Dst) {
    // We will use these as output buffers, as well as an input/output
    // buffers for the padded Nemesis input.
    stringstream src(ios::in | ios::out | ios::binary);

    // Copy to buffer.
    src << Src.rdbuf();

    // Pad source with zeroes until it is a multiple of 32 bits.
    if ((src.tellp() & 0x1f) != 0) {
        fill_n(ostreambuf_iterator<char>(src), 32 - (src.tellp() & 0x1f), 0);
    }
    size_t const sz = src.tellp();

    // Now we will build the alternating bit stream for mode 1 compression.
    src.clear();
    src.seekg(0);

    string sin = src.str();
    for (size_t i = sin.size() - 4; i > 0; i -= 4) {
        sin[i + 0] ^= sin[i - 4];
        sin[i + 1] ^= sin[i - 3];
        sin[i + 2] ^= sin[i - 2];
        sin[i + 3] ^= sin[i - 1];
    }
    stringstream alt(sin, ios::in | ios::out | ios::binary);

    stringstream buffers[4];
    size_t       sizes[4];

    // Four different attempts to encode, for improved file size.
    sizes[0] = nemesis_internal::encode(src, buffers[0], 0, sz, Compare_node());
    sizes[1]
            = nemesis_internal::encode(src, buffers[1], 0, sz, Compare_node2());
    sizes[2] = nemesis_internal::encode(alt, buffers[2], 1, sz, Compare_node());
    sizes[3]
            = nemesis_internal::encode(alt, buffers[3], 1, sz, Compare_node2());

    // Figure out what was the best encoding.
    size_t bestsz     = numeric_limits<size_t>::max();
    size_t beststream = 0;
    for (size_t ii = 0; ii < sizeof(sizes) / sizeof(sizes[0]); ii++) {
        if (sizes[ii] < bestsz) {
            bestsz     = sizes[ii];
            beststream = ii;
        }
    }

    buffers[beststream].seekg(0);
    Dst << buffers[beststream].rdbuf();
    return true;
}

bool nemesis::encode(
        std::ostream& Dst, uint8_t const* data, size_t const Size) {
    stringstream Src(ios::in | ios::out | ios::binary);
    Src.write(reinterpret_cast<char const*>(data), Size);
    Src.seekg(0);
    return encode(Src, Dst);
}
