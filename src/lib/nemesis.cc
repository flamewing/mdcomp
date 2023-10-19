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

#include "mdcomp/nemesis.hh"

#include "mdcomp/bigendian_io.hh"
#include "mdcomp/bitstream.hh"
#include "mdcomp/ignore_unused_variable_warning.hh"
#include "mdcomp/stream_utils.hh"

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <istream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <queue>
#include <ranges>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

template <typename Enum>
concept Enumerator = requires() { requires(std::is_enum_v<Enum>); };

template <Enumerator Enum>
constexpr std::underlying_type_t<Enum> to_underlying(Enum value) {
    return static_cast<std::underlying_type_t<Enum>>(value);
};

// This represents a nibble run of up to 7 repetitions of the starting nibble.
class nibble_run {
private:
    std::byte nibble{0};    // Nibble we are interested in.
    uint8_t   count{0};     // How many times the nibble is repeated.

public:
    // Constructors.
    nibble_run() noexcept = default;

    nibble_run(std::byte nibble_in, size_t count_in) noexcept
            : nibble(nibble_in), count(static_cast<uint8_t>(count_in)) {}

    // Sorting operator.
    [[nodiscard]] std::strong_ordering operator<=>(nibble_run const& other) const noexcept
            = default;

    // Getters/setters for all properties.
    [[nodiscard]] std::byte get_nibble() const noexcept {
        return nibble;
    }

    [[nodiscard]] size_t get_count() const noexcept {
        return count;
    }

    void set_nibble(std::byte const value) noexcept {
        nibble = value;
    }

    void enlarge() noexcept {
        count++;
    }
};

struct size_freq_nibble {
    size_t     count{0};
    nibble_run nibble;
    size_t     code_length{0};

    size_freq_nibble(
            size_t count_in, nibble_run const& nibble_in, size_t const length) noexcept
            : count(count_in), nibble(nibble_in), code_length(length) {}

    size_freq_nibble() noexcept = default;
};

struct bit_code {
    size_t code;
    size_t length;

    [[nodiscard]] std::strong_ordering operator<=>(bit_code const&) const noexcept
            = default;
};

using code_size_map   = std::map<nibble_run, size_t>;
using run_count_map   = std::map<nibble_run, size_t>;
using nibble_code_map = std::map<nibble_run, bit_code>;
using code_nibble_map = std::map<bit_code, nibble_run>;

// Slightly based on code by Mark Nelson for Huffman encoding.
// https://marknelson.us/posts/1996/01/01/priority-queues.html
// This represents a node (leaf or branch) in the Huffman encoding tree.
class node : public std::enable_shared_from_this<node> {
private:
    std::shared_ptr<node> left_child, right_child;
    size_t                weight;
    nibble_run            value{std::byte{0}, 0};

public:
    // Construct a new leaf node for character c.
    node(nibble_run const& value_in, size_t const weight_in) noexcept
            : weight(weight_in), value(value_in) {}

    // Construct a new internal node that has children c1 and c2.
    node(std::shared_ptr<node> const& left_child_in,
         std::shared_ptr<node> const& right_child_in) noexcept
            : left_child(left_child_in), right_child(right_child_in),
              weight(left_child_in->weight + right_child_in->weight) {}

    // Free the memory used by the child nodes.
    void prune() noexcept {
        left_child.reset();
        right_child.reset();
    }

    // Comparison operators.
    [[nodiscard]] std::weak_ordering operator<=>(node const& other) const noexcept {
        return weight <=> other.weight;
    }

    // This tells if the node is a leaf or a branch.
    [[nodiscard]] bool is_leaf() const noexcept {
        return left_child == nullptr && right_child == nullptr;
    }

    // Getters/setters for all properties.
    [[nodiscard]] std::shared_ptr<node const> get_child0() const noexcept {
        return left_child;
    }

    [[nodiscard]] std::shared_ptr<node const> get_child1() const noexcept {
        return right_child;
    }

    [[nodiscard]] size_t get_weight() const noexcept {
        return weight;
    }

    [[nodiscard]] nibble_run const& get_value() const noexcept {
        return value;
    }

    void set_left_child(std::shared_ptr<node> left_child_in) noexcept {
        left_child = std::move(left_child_in);
    }

    void set_right_child(std::shared_ptr<node> right_child_in) noexcept {
        right_child = std::move(right_child_in);
    }

    void set_weight(size_t weight_in) noexcept {
        weight = weight_in;
    }

    void set_value(nibble_run const& value_in) noexcept {
        value = value_in;
    }

    // This goes through the tree, starting with the current node, generating
    // a map associating a nibble run with its code length.
    void traverse(code_size_map& sizemap) const noexcept {
        if (is_leaf()) {
            sizemap[value] += 1;
        } else {
            if (left_child) {
                left_child->traverse(sizemap);
            }
            if (right_child) {
                right_child->traverse(sizemap);
            }
        }
    }
};

using node_vector = std::vector<std::shared_ptr<node>>;

struct compare_size {
    bool operator()(
            size_freq_nibble const& left, size_freq_nibble const& right) const noexcept {
        if (left.code_length < right.code_length) {
            return true;
        }
        if (left.code_length > right.code_length) {
            return false;
        }
        // rhs.code_length == lhs.code_length
        if (left.count > right.count) {
            return true;
        }
        if (left.count < right.count) {
            return false;
        }
        // rhs.count == lhs.count
        nibble_run const& left_nibble  = left.nibble;
        nibble_run const  right_nibble = right.nibble;
        return (left_nibble.get_nibble() < right_nibble.get_nibble()
                || (left_nibble.get_nibble() == right_nibble.get_nibble()
                    && left_nibble.get_count() > right_nibble.get_count()));
    }
};

#define CUSTOM_COMPARE 1

struct compare_node {
    bool operator()(std::shared_ptr<node> const& left, std::shared_ptr<node> const& right)
            const noexcept {
#if CUSTOM_COMPARE
        if (*left > *right) {
            return true;
        }
        if (*left < *right) {
            return false;
        }
        return left->get_value().get_count() < right->get_value().get_count();
#else
        return *left > *right;
#endif
    }

    // Just discard the lowest weighted item.
    void update(node_vector& nodes, nibble_code_map& codes) const noexcept {
        ignore_unused_variable_warning(this, nodes, codes);
    }

    // Initialize
    void initialize(node_vector& nodes, nibble_code_map& codes) const noexcept {
        ignore_unused_variable_warning(codes);
        std::ranges::make_heap(nodes, *this);
    }
};

struct compare_node2 {
    nibble_code_map* code_map = nullptr;

    bool operator()(std::shared_ptr<node> const& left, std::shared_ptr<node> const& right)
            const noexcept {
        if (code_map == nullptr || code_map->empty()) {
            if (*left < *right) {
                return true;
            }
            if (*left > *right) {
                return false;
            }
            return left->get_value().get_count() > right->get_value().get_count();
        }
        nibble_run const left_nibble  = left->get_value();
        nibble_run const right_nibble = right->get_value();

        auto get_len = [&](std::shared_ptr<node> const& node, nibble_run nibble) {
            if (auto const iter = code_map->find(nibble); iter != code_map->cend()) {
                size_t const bit_count = (iter->second).length;
                return (bit_count & 0x7fU) * node->get_weight() + 16;
            }
            return (6 + 7) * node->get_weight();
        };

        size_t const left_code_length  = get_len(left, left_nibble);
        size_t const right_code_length = get_len(right, right_nibble);
        if (left_code_length > right_code_length) {
            return true;
        }
        if (left_code_length < right_code_length) {
            return false;
        }

        size_t const left_weighted_count
                = (left_nibble.get_count() + 1) * left->get_weight();
        size_t const right_weighted_count
                = (right_nibble.get_count() + 1) * right->get_weight();
        if (left_weighted_count < right_weighted_count) {
            return true;
        }
        if (left_weighted_count > right_weighted_count) {
            return false;
        }
        return left_nibble.get_count() < right_nibble.get_count();
    }

    // Resort the heap using weights from the previous iteration, then discards
    // the lowest weighted item.
    void update(node_vector& nodes, nibble_code_map& codes) noexcept {
        *code_map = codes;
        std::ranges::make_heap(nodes, *this);
    }

    void initialize(node_vector& nodes, nibble_code_map& codes) noexcept {
        update(nodes, codes);
    }
};

template <>
size_t moduled_nemesis::pad_mask_bits = 1U;

using nem_ibitstream = ibitstream<uint8_t, bit_endian::big, big_endian, true>;
using nem_obitstream = obitstream<uint8_t, bit_endian::big, big_endian>;

class nemesis_internal {
public:
    static void decode_header(std::istream& source, code_nibble_map& code_map) {
        // storage for output value to decompression buffer
        std::byte out_val{0};

        // main loop. Header is terminated by the value of 0xFF
        for (size_t in_val = read1(source); in_val != 0xFF; in_val = read1(source)) {
            // if most significant bit is set, store the last 4 bits and discard
            // the rest
            if ((in_val & 0x80U) != 0) {
                out_val = static_cast<std::byte>(in_val & 0xfU);
                in_val  = read1(source);
            }

            nibble_run const run(out_val, ((in_val & 0x70U) >> 4U) + 1);

            size_t const code   = read1(source);
            size_t const length = in_val & 0xfU;
            // Read the run's code from stream.
            code_map[bit_code{code, length}] = run;
        }
    }

    static void decode(
            std::istream& source, std::ostream& dest, code_nibble_map& code_map,
            size_t const num_tiles, bool const alt_out = false) {
        // This buffer is used for alternating mode decoding.
        std::stringstream str_dest(std::ios::in | std::ios::out | std::ios::binary);

        // Set bit I/O streams.
        nem_ibitstream bits(source);
        nem_obitstream output(str_dest);

        size_t code   = bits.pop();
        size_t length = 1;

        // When to stop decoding: number of tiles * $20 bytes per tile * 8 bits
        // per byte.
        size_t const total_bits   = num_tiles << 8U;
        size_t       bits_written = 0;
        while (bits_written < total_bits) {
            if (code == 0x3f && length == 6) {
                // Bit pattern %111111; inline RLE.
                // First 3 bits are repetition count, followed by the inlined
                // nibble.
                size_t  count  = bits.read(3) + 1;
                uint8_t nibble = bits.read(4);
                bits_written += count * 4;

                // Write single nibble if needed.
                if ((count % 2) != 0) {
                    output.write(nibble, 4);
                }

                // Now write pairs of nibbles.
                count >>= 1U;
                nibble |= static_cast<uint8_t>(nibble << 4U);
                for (size_t i = 0; i < count; i++) {
                    output.write(nibble, 8);
                }

                if (bits_written >= total_bits) {
                    break;
                }

                // Read next bit, replacing previous data.
                code   = bits.pop();
                length = 1;
            } else {
                // Find out if the data so far is a nibble code.
                if (auto const iter = code_map.find(bit_code{code, length});
                    iter != code_map.cend()) {
                    // If it is, then it is time to output the encoded nibble
                    // run.
                    nibble_run const& run = iter->second;

                    std::byte nibble = run.get_nibble();
                    size_t    count  = run.get_count();
                    bits_written += count * 4;

                    // Write single nibble if needed.
                    if ((count % 2) != 0) {
                        output.write(static_cast<uint8_t>(nibble), 4);
                    }

                    // Now write pairs of nibbles.
                    count >>= 1U;
                    nibble |= (nibble << 4U);
                    for (size_t i = 0; i < count; i++) {
                        output.write(static_cast<uint8_t>(nibble), 8);
                    }

                    if (bits_written >= total_bits) {
                        break;
                    }

                    // Read next bit, replacing previous data.
                    code   = bits.pop();
                    length = 1;
                } else {
                    // Read next bit and append to current data.
                    code = (code << 1U) | bits.pop();
                    length++;
                }
            }
        }

        // Write out any remaining bits, padding with zeroes.
        output.flush();

        auto const final_size = static_cast<std::streamsize>(num_tiles << 5U);
        if (alt_out) {
            // For alternating decoding, we must now incrementally XOR and
            // output the lines.
            str_dest.seekg(0);
            str_dest.clear();
            uint32_t value = source_endian::read4(str_dest);
            source_endian::write4(dest, value);
            while (str_dest.tellg() < final_size) {
                value ^= source_endian::read4(str_dest);
                source_endian::write4(dest, value);
            }
        } else {
            dest.write(str_dest.str().c_str(), final_size);
        }
    }

    template <size_t N>
    using row_t = std::array<size_t, N>;
    template <size_t M, size_t N>
    using matrix_t = std::array<row_t<N>, M>;

    static size_t estimate_file_size(nibble_code_map& code_map, run_count_map& counts) {
        // We now compute the final file size for this code table.
        // 2 bytes at the start of the file, plus 1 byte at the end of the
        // code table.
        size_t    size_estimate = size_t{3} * 8;
        std::byte last{0xff};
        // Start with any nibble runs with their own code.
        for (auto const& [run, code] : code_map) {
            // Each new nibble needs an extra byte.
            if (last != run.get_nibble()) {
                size_estimate += 8;
                // Be sure to SET the last nibble to the current nibble... this
                // fixes a bug that caused file sizes to increase in some cases.
                last = run.get_nibble();
            }
            // 2 bytes per nibble run in the table.
            size_estimate += size_t{2U} * 8U;
            // How many bits this nibble run uses in the file.
            size_estimate += counts[run] * code.length;
        }

        // Supplementary code map for the nibble runs that can be broken up into
        // shorter nibble runs with a smaller bit length than inlining.
        nibble_code_map extra_code_map;
        // Now we will compute the size requirements for inline nibble runs.
        for (auto const& [run, frequency] : counts) {
            // Find out if this nibble run has a code for it.
            auto found_run = code_map.find(run);
            if (found_run == code_map.cend()) {
                // Nibble run does not have its own code. We need to find out if
                // we can break it up into smaller nibble runs with total code
                // size less than 13 bits or if we need to inline it (13 bits).
                if (run.get_count() == 0) {
                    // If this is a nibble run with zero repeats, we can't break
                    // it up into smaller runs, so we inline it.
                    size_estimate += (6 + 7) * frequency;
                } else if (run.get_count() == 1) {
                    // We stand a chance of breaking the nibble run.

                    // This case is rather trivial, so we hard-code it.
                    // We can break this up only as 2 consecutive runs of a
                    // nibble run with count == 0.
                    nibble_run const target{run.get_nibble(), 0};
                    found_run = code_map.find(target);
                    if (found_run == code_map.cend() || (found_run->second).length > 6) {
                        // The smaller nibble run either does not have its own
                        // code or it results in a longer bit code when doubled
                        // up than would result from inlining the run. In either
                        // case, we inline the nibble run.
                        size_estimate += (6 + 7) * frequency;
                    } else {
                        // The smaller nibble run has a small enough code that
                        // it is more efficient to use it twice than to inline
                        // our nibble run. So we do exactly that, by adding a
                        // (temporary) entry in the supplementary code_map, which
                        // will later be merged into the main code_map.
                        size_t code   = (found_run->second).code;
                        size_t length = (found_run->second).length;
                        code          = (code << length) | code;
                        length <<= 1U;
                        size_estimate += length * frequency;
                        length |= 0x80U;    // Flag this as a false code.
                        extra_code_map[run] = bit_code{code, length};
                    }
                } else {
                    // We stand a chance of breaking it the nibble run.

                    size_t const count = run.get_count();
                    // Pointer to table of linear coefficients. This table has N
                    // columns for each line.
                    auto const linear_coefficients = [&]() {
                        // This is a linear optimization problem subjected to 2
                        // constraints. If the number of repeats of the current
                        // nibble run is N, then we have N dimensions.
                        // Here are some hard-coded tables, obtained by brute-force:
                        constexpr static matrix_t<2, 2> const linear_coefficients2{
                                row_t<2>{3, 0},
                                row_t<2>{1, 1}
                        };
                        constexpr static matrix_t<4, 3> const linear_coefficients3{
                                row_t<3>{4, 0, 0},
                                row_t<3>{2, 1, 0},
                                row_t<3>{1, 0, 1},
                                row_t<3>{0, 2, 0}
                        };
                        constexpr static matrix_t<6, 4> const linear_coefficients4{
                                row_t<4>{5, 0, 0, 0},
                                row_t<4>{3, 1, 0, 0},
                                row_t<4>{2, 0, 1, 0},
                                row_t<4>{1, 2, 0, 0},
                                row_t<4>{1, 0, 0, 1},
                                row_t<4>{0, 1, 1, 0}
                        };
                        constexpr static matrix_t<10, 5> const linear_coefficients5{
                                row_t<5>{6, 0, 0, 0, 0},
                                row_t<5>{4, 1, 0, 0, 0},
                                row_t<5>{3, 0, 1, 0, 0},
                                row_t<5>{2, 2, 0, 0, 0},
                                row_t<5>{2, 0, 0, 1, 0},
                                row_t<5>{1, 1, 1, 0, 0},
                                row_t<5>{1, 0, 0, 0, 1},
                                row_t<5>{0, 3, 0, 0, 0},
                                row_t<5>{0, 1, 0, 1, 0},
                                row_t<5>{0, 0, 2, 0, 0}
                        };
                        constexpr static matrix_t<14, 6> const linear_coefficients6{
                                row_t<6>{7, 0, 0, 0, 0, 0},
                                row_t<6>{5, 1, 0, 0, 0, 0},
                                row_t<6>{4, 0, 1, 0, 0, 0},
                                row_t<6>{3, 2, 0, 0, 0, 0},
                                row_t<6>{3, 0, 0, 1, 0, 0},
                                row_t<6>{2, 1, 1, 0, 0, 0},
                                row_t<6>{2, 0, 0, 0, 1, 0},
                                row_t<6>{1, 3, 0, 0, 0, 0},
                                row_t<6>{1, 1, 0, 1, 0, 0},
                                row_t<6>{1, 0, 2, 0, 0, 0},
                                row_t<6>{1, 0, 0, 0, 0, 1},
                                row_t<6>{0, 2, 1, 0, 0, 0},
                                row_t<6>{0, 1, 0, 0, 1, 0},
                                row_t<6>{0, 0, 1, 1, 0, 0}
                        };
                        constexpr static matrix_t<21, 7> const linear_coefficients7{
                                row_t<7>{8, 0, 0, 0, 0, 0, 0},
                                row_t<7>{6, 1, 0, 0, 0, 0, 0},
                                row_t<7>{5, 0, 1, 0, 0, 0, 0},
                                row_t<7>{4, 2, 0, 0, 0, 0, 0},
                                row_t<7>{4, 0, 0, 1, 0, 0, 0},
                                row_t<7>{3, 1, 1, 0, 0, 0, 0},
                                row_t<7>{3, 0, 0, 0, 1, 0, 0},
                                row_t<7>{2, 3, 0, 0, 0, 0, 0},
                                row_t<7>{2, 1, 0, 1, 0, 0, 0},
                                row_t<7>{2, 0, 2, 0, 0, 0, 0},
                                row_t<7>{2, 0, 0, 0, 0, 1, 0},
                                row_t<7>{1, 2, 1, 0, 0, 0, 0},
                                row_t<7>{1, 1, 0, 0, 1, 0, 0},
                                row_t<7>{1, 0, 1, 1, 0, 0, 0},
                                row_t<7>{1, 0, 0, 0, 0, 0, 1},
                                row_t<7>{0, 4, 0, 0, 0, 0, 0},
                                row_t<7>{0, 2, 0, 1, 0, 0, 0},
                                row_t<7>{0, 1, 2, 0, 0, 0, 0},
                                row_t<7>{0, 1, 0, 0, 0, 1, 0},
                                row_t<7>{0, 0, 1, 0, 1, 0, 0},
                                row_t<7>{0, 0, 0, 2, 0, 0, 0}
                        };
                        // Get correct coefficient table:
                        switch (count) {
                        case 2:
                            return std::span{
                                    linear_coefficients2[0].data(),
                                    linear_coefficients2.size()};
                        case 3:
                            return std::span{
                                    linear_coefficients3[0].data(),
                                    linear_coefficients3.size()};
                        case 4:
                            return std::span{
                                    linear_coefficients4[0].data(),
                                    linear_coefficients4.size()};
                        case 5:
                            return std::span{
                                    linear_coefficients5[0].data(),
                                    linear_coefficients5.size()};
                        case 6:
                            return std::span{
                                    linear_coefficients6[0].data(),
                                    linear_coefficients6.size()};
                        case 7:
                        default:
                            return std::span{
                                    linear_coefficients7[0].data(),
                                    linear_coefficients7.size()};
                        }
                    }();

                    std::byte const nibble = run.get_nibble();
                    // Vector containing the code length of each nibble run, or
                    // 13 if the nibble run is not in the code_map.
                    std::vector<size_t> run_length;
                    // Init vector.
                    for (size_t i = 0; i < count; i++) {
                        // Is this run in the code_map?
                        nibble_run const target(nibble, i);
                        auto             target_iter = code_map.find(target);
                        if (target_iter == code_map.cend()) {
                            // It is not.
                            // Put inline length in the vector.
                            run_length.push_back(6 + 7);
                        } else {
                            // It is.
                            // Put code length in the vector.
                            run_length.push_back((target_iter->second).length);
                        }
                    }

                    // Now go through the linear coefficient table and tally up
                    // the total code size, looking for the best case.
                    // The best size is initialized to be the inlined case.
                    size_t best_size = 6 + 7;
                    size_t base      = 0;

                    std::optional<size_t> best_line;

                    for (size_t i = 0; i < linear_coefficients.size();
                         i++, base += count) {
                        // Tally up the code length for this coefficient line.
                        size_t length = 0;
                        for (size_t j = 0; j < count; j++) {
                            size_t const coeff = linear_coefficients[base + j];
                            if (coeff == 0U) {
                                continue;
                            }

                            length += coeff * run_length[j];
                        }
                        // Is the length better than the best yet?
                        if (length < best_size) {
                            // If yes, store it as the best.
                            best_size = length;
                            best_line = base;
                        }
                    }
                    // Have we found a better code than inlining?
                    if (best_line) {
                        auto const best_base = *best_line;
                        // We have; use it. To do so, we have to build the code
                        // and add it to the supplementary code table.
                        size_t code   = 0;
                        size_t length = 0;
                        for (size_t i = 0; i < count; i++) {
                            size_t const coeff = linear_coefficients[best_base + i];
                            if (coeff == 0U) {
                                continue;
                            }
                            // Is this run in the code_map?
                            nibble_run const target(nibble, i);
                            auto             target_iter = code_map.find(target);
                            if (target_iter != code_map.cend()) {
                                // It is; it MUST be, as the other case is
                                // impossible by construction.
                                for (size_t j = 0; j < coeff; j++) {
                                    length += (target_iter->second).length;
                                    code <<= (target_iter->second).length;
                                    code |= (target_iter->second).code;
                                }
                            }
                        }
                        if (length != best_size) {
                            // ERROR! DANGER! THIS IS IMPOSSIBLE!
                            // But just in case...
                            size_estimate += (6 + 7) * frequency;
                        } else {
                            // By construction, best_size is at most 12.
                            // Flag it as a false code.
                            size_t const mlen = best_size | 0x80U;
                            // Add it to supplementary code map.
                            extra_code_map[run] = bit_code{code, mlen};
                            size_estimate += best_size * frequency;
                        }
                    } else {
                        // No, we will have to inline it.
                        size_estimate += (6 + 7) * frequency;
                    }
                }
            }
        }
        code_map.insert(
                std::ranges::cbegin(extra_code_map), std::ranges::cend(extra_code_map));

        // Round up to a full byte.
        size_estimate = (size_estimate + 7) & ~7U;

        return size_estimate;
    }

    enum class nemesis_mode : uint16_t {
        normal          = 0U,
        progressive_xor = 1U << 15U,
    };

    friend uint16_t operator|(nemesis_mode mode, std::streamoff length) {
        return static_cast<uint16_t>(to_underlying(mode) | static_cast<uint16_t>(length));
    }

    template <typename Compare>
    static std::streamoff encode(
            std::istream& source, std::ostream& dest, nemesis_mode mode,
            std::streamoff const length, Compare&& comp) {
        auto compare = std::forward<Compare>(comp);
        // Seek to start and clear all errors.
        source.clear();
        source.seekg(0);

        // Build RLE nibble runs, RLE-encoding the nibble runs as we go along.
        // Maximum run length is 8, meaning 7 repetitions.
        auto [rle_source, count_map] = [&]() {
            // Unpack source so we don't have to deal with nibble IO after.
            std::vector<nibble_run>   rle_src;
            run_count_map             counts;
            std::vector<std::byte>    unpack;
            constexpr const std::byte low_nibble{0xfU};
            // TODO: Make this through a lazy smart iterator so it can be
            // bundled in the loop below.
            for (std::streamoff i = 0; i < length; i++) {
                std::byte const value{read1(source)};
                unpack.emplace_back((value >> 4U) & low_nibble);
                unpack.emplace_back(value & low_nibble);
            }
            // Sentinel for simplifying logic
            unpack.emplace_back(std::byte{0xff});

            nibble_run curr{unpack[0], 0};
            for (auto const next : unpack | std::views::drop(1)) {
                if (next != curr.get_nibble() || curr.get_count() >= 7) {
                    rle_src.push_back(curr);
                    counts[curr] += 1;
                    curr = {next, 0};
                } else {
                    curr.enlarge();
                }
            }
            return std::pair(rle_src, counts);
        }();
        // We will use the Package-merge algorithm to build the optimal
        // length-limited Huffman code for the current file. To do this, we must
        // map the current problem onto the Coin Collector's problem. Build the
        // basic coin collection.

        // No point in including anything with weight less than 2, as they
        // would actually increase compressed file size if used.
        constexpr auto freq_filter = [](auto&& kv_pair) noexcept {
            return kv_pair.second > 1;
        };
        constexpr auto to_node = [](auto&& kv_pair) {
            auto const& [run, frequency] = kv_pair;
            return std::make_shared<node>(run, frequency);
        };
        auto nodes = count_map | std::views::filter(freq_filter)
                     | std::views::transform(to_node) | detail::to<node_vector>();

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
        nibble_code_map code_map;

        // This may seem useless, but my tests all indicate that this reduces
        // the average file size. I haven't the foggiest idea why.
        compare.initialize(nodes, code_map);

        // Size estimate. This is used to build the optimal compressed file.
        size_t size_est = 0xffffffff;

        // We will solve the Coin Collector's problem several times, each time
        // ignoring more of the least frequent nibble runs. This allows us to
        // find *the* lowest file size.
        while (nodes.size() > 1) {
            // Make a copy of the basic coin collection.
            using coin_queue = std::priority_queue<
                    std::shared_ptr<node>, node_vector, compare_node>;
            coin_queue const base_coins(
                    std::ranges::cbegin(nodes), std::ranges::cend(nodes));

            // We now solve the Coin collector's problem using the Package-merge
            // algorithm. The solution goes here.
            node_vector solution;
            // This holds the packages from the last iteration.
            coin_queue current_coins(base_coins);
            size_t     target = (base_coins.size() - 1) << 8U;
            size_t     index  = 0;
            while (target != 0) {
                // Gets lowest bit set in its proper place:
                size_t const value = (target & -target);
                size_t const cost  = 1U << index;
                // Is the current denomination equal to the least denomination?
                if (cost == value) {
                    // If yes, take the least valuable node and put it into the
                    // solution.
                    solution.push_back(current_coins.top());
                    current_coins.pop();
                    target -= cost;
                }

                // The coin collection has coins of values 1 to 8; copy from the
                // original in those cases for the next step.
                coin_queue next_coins;
                if (index < 7) {
                    next_coins = base_coins;
                }

                // Split the current list into pairs and insert the packages
                // into the next list.
                while (current_coins.size() > 1) {
                    auto child1 = current_coins.top();
                    current_coins.pop();
                    auto child0 = current_coins.top();
                    current_coins.pop();
                    next_coins.push(make_shared<node>(child0, child1));
                }
                index++;
                current_coins = next_coins;
            }

            // The Coin Collector's problem has been solved. Now it is time to
            // map the solution back into the length-limited Huffman coding
            // problem.

            // To do that, we iterate through the solution and count how many
            // times each nibble run has been used (remember that the coin
            // collection had multiple coins associated with each nibble run) --
            // this number is the optimal bit length for the nibble run for the
            // current coin collection.
            code_size_map base_size_map;
            for (auto& elem : solution) {
                elem->traverse(base_size_map);
            }

            // With the length-limited Huffman coding problem solved, it is now
            // time to build the code table. As input, we have a map associating
            // a nibble run to its optimal encoded bit length. We will build the
            // codes using the canonical Huffman code.

            // To do that, we will need to know how many codes we will need for
            // any given code length. Since there are only 8 valid code lengths,
            // we only need this simple array.
            std::array<size_t, 8> size_counts{0};
            // This set contains lots more information, and is used to associate
            // the nibble run with its optimal code. It is sorted by code size,
            // then by frequency of the nibble run, then by the nibble run.
            using size_set = std::multiset<size_freq_nibble, compare_size>;
            size_set sizemap;
            for (auto const& [run, size] : base_size_map) {
                size_t const count = count_map[run];
                size_counts[size - 1]++;
                sizemap.emplace(count, run, size);
            }

            // We now build the canonical Huffman code table.
            // "base" is the code for the first nibble run with a given bit
            // length. "carry" is how many nibble runs were demoted to a higher
            // bit length at an earlier step. "cnt" is how many nibble runs have
            // a given bit length.
            size_t base  = 0;
            size_t carry = 0;
            // This vector contains the codes sorted by size.
            std::vector<bit_code> codes;
            for (size_t i = 1; i <= 8; i++) {
                // How many nibble runs have the desired bit length.
                size_t       count = size_counts[i - 1] + carry;
                size_t const mask  = (size_t{1} << i) - 1;
                size_t const mask2
                        = (i > 6) ? (mask & ~((size_t{1} << (i - 6U)) - 1)) : 0;
                carry = 0;
                for (size_t j = 0; j < count; j++) {
                    // Sequential binary numbers for codes.
                    size_t const code = base + j;
                    // We do not want any codes composed solely of 1's or which
                    // start with 111111, as that sequence is reserved.
                    if ((i <= 6 && code == mask) || (i > 6 && code == mask2)) {
                        // We must demote this many nibble runs to a longer
                        // code.
                        carry = count - j;
                        count = j;
                        break;
                    }
                    codes.emplace_back(bit_code{code, i});
                }
                // This is the beginning bit pattern for the next bit length.
                base = (base + count) << 1U;
            }

            // With the canonical table build, the code_map can finally be built.
            nibble_code_map temp_code_map;
            size_t          position = 0;
            for (auto it = std::ranges::cbegin(sizemap);
                 it != std::ranges::cend(sizemap) && position < codes.size();
                 ++it, position++) {
                temp_code_map[it->nibble] = codes[position];
            }

            // We now compute the final file size for this code table.
            size_t const temp_size_est = estimate_file_size(temp_code_map, count_map);

            // This may resort the items. After that, it will discard the lowest
            // weighted item.
            compare.update(nodes, temp_code_map);
            std::ranges::pop_heap(nodes, compare);
            nodes.pop_back();

            // Is this iteration better than the best?
            if (temp_size_est < size_est) {
                // If yes, save the code_map and file size.
                code_map = temp_code_map;
                size_est = temp_size_est;
            }
        }
        // Special case.
        if (nodes.size() == 1) {
            nibble_code_map             temp_code_map;
            std::shared_ptr<node> const child = nodes.front();
            temp_code_map[child->get_value()] = bit_code{0U, 1};
            size_t const temp_size_est = estimate_file_size(temp_code_map, count_map);

            // Is this iteration better than the best?
            if (temp_size_est < size_est) {
                // If yes, save the code_map and file size.
                code_map = temp_code_map;
                size_est = temp_size_est;
            }
        }
        // This is no longer needed.
        count_map.clear();

        // We now have a prefix-free code map associating the RLE-encoded nibble
        // runs with their code. Now we write the file.
        // Write header.
        big_endian::write2(dest, mode | (length / 32));
        std::byte last_nibble{0xff};
        for (auto const& [run, bit_code] : code_map) {
            auto const [code, size] = bit_code;
            // length with bit 7 set is a special device for further reducing file
            // size, and should NOT be on the table.
            if ((size & 0x80U) != 0) {
                continue;
            }
            if (run.get_nibble() != last_nibble) {
                // 0x80 marks byte as setting a new nibble.
                write1(dest, 0x80U | static_cast<uint8_t>(run.get_nibble()));
                last_nibble = run.get_nibble();
            }

            write1(dest, static_cast<uint8_t>(run.get_count() << 4U | size));
            write1(dest, static_cast<uint8_t>(code));
        }

        // Mark end of header.
        write1(dest, 0xff);

        // Time to write the encoded bitstream.
        nem_obitstream bits(dest);

        // The RLE-encoded source makes for a far faster encode as we simply
        // use the nibble runs as an index into the map, meaning a quick binary
        // search gives us the code to use (if in the map) or tells us that we
        // need to use inline RLE.
        for (auto& run : rle_source) {
            auto value = code_map.find(run);
            if (value != code_map.cend()) {
                size_t const code  = (value->second).code;
                size_t       count = (value->second).length;
                // length with bit 7 set is a device to bypass the code table at
                // the start of the file. We need to clear the bit here before
                // writing the code to the file.
                count &= 0x7fU;
                // We can have codes in the 9-12 range due to the break up of
                // large inlined runs into smaller non-inlined runs. Deal with
                // those high bits first, if needed.
                if (count > 8) {
                    bits.write((code >> 8U) & 0xffU, count - 8);
                    count = 8;
                }
                bits.write(static_cast<uint8_t>(code & 0xffU), count);
            } else {
                bits.write(0x3f, 6);
                bits.write(static_cast<uint8_t>(run.get_count()), 3);
                bits.write(static_cast<uint8_t>(run.get_nibble()), 4);
            }
        }
        // Fill remainder of last byte with zeroes and write if needed.
        bits.flush();
        return dest.tellp();
    }
};

bool nemesis::decode(std::istream& source, std::ostream& dest) {
    code_nibble_map code_map;
    size_t          num_tiles = big_endian::read2(source);
    // sets the output mode based on the value of the first bit
    bool const xor_mode = (num_tiles & 0x8000U) != 0;
    num_tiles &= 0x7fffU;

    if (num_tiles > 0) {
        nemesis_internal::decode_header(source, code_map);
        nemesis_internal::decode(source, dest, code_map, num_tiles, xor_mode);
    }
    return true;
}

bool nemesis::encode(std::istream& source, std::ostream& dest) {
    // We will use these as output buffers, as well as an input/output
    // buffers for the padded Nemesis input.
    std::stringstream str_source(std::ios::in | std::ios::out | std::ios::binary);

    // Copy to buffer.
    str_source << source.rdbuf();

    // Pad source with zeroes until it is a multiple of 32 bytes.
    detail::pad_to_multiple(str_source, 32);
    auto const size = str_source.tellp();

    // Now we will build the alternating bit stream for mode 1 compression.
    std::stringstream source_xor(std::ios::in | std::ios::out | std::ios::binary);
    str_source.clear();
    str_source.seekg(0);

    uint32_t value = 0;
    while (source_xor.tellp() < size) {
        uint32_t const new_value = source_endian::read4(str_source);
        source_endian::write4(source_xor, value ^ new_value);
        value = new_value;
    }

    std::array<std::stringstream, 4> buffers;
    using nemesis_mode = nemesis_internal::nemesis_mode;
    // Four different attempts to encode, for improved file size.
    std::array sizes{
            nemesis_internal::encode(
                    str_source, buffers[0], nemesis_mode::normal, size, compare_node{}),
            nemesis_internal::encode(
                    str_source, buffers[1], nemesis_mode::normal, size, compare_node2{}),
            nemesis_internal::encode(
                    source_xor, buffers[2], nemesis_mode::progressive_xor, size,
                    compare_node{}),
            nemesis_internal::encode(
                    source_xor, buffers[3], nemesis_mode::progressive_xor, size,
                    compare_node2{})};

    // Figure out what was the best encoding.
    std::streamoff best_size   = std::numeric_limits<std::streamoff>::max();
    size_t         best_stream = 0;
    for (size_t ii = 0; ii < sizes.size(); ii++) {
        if (sizes[ii] < best_size) {
            best_size   = sizes[ii];
            best_stream = ii;
        }
    }

    buffers[best_stream].seekg(0);
    dest << buffers[best_stream].rdbuf();
    return true;
}

bool nemesis::encode(std::ostream& dest, std::span<uint8_t const> data) {
    std::stringstream source(std::ios::in | std::ios::out | std::ios::binary);
    source.write(reinterpret_cast<char const*>(data.data()), std::ssize(data));
    source.seekg(0);
    return encode(source, dest);
}
