/*
 * Copyright (C) Flamewing 2013-2025 <flamewing.sonic@gmail.com>
 *
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

#ifndef LIB_LZSS_HH
#define LIB_LZSS_HH

#include "mdcomp/bigendian_io.hh"
#include "mdcomp/bitstream.hh"
#include "mdcomp/forge_span.hh"
#include "mdcomp/stream_utils.hh"
#include "mdcomp/unreachable.hh"

#include <boost/container/container_fwd.hpp>
#include <boost/container/static_vector.hpp>

#include <algorithm>
#include <cassert>
#include <concepts>    // IWYU pragma: keep
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace lzss {
    /*
     * Class representing an edge in the LZSS-compression graph. An edge (u, v)
     * indicates that there is a sliding window match that covers all the characters
     * in the [u, v) range (half-open) -- that is, node v is not part of the match.
     * Each node is a character in the file, and is represented by its position.
     */
    template <typename Adaptor>
    class adj_list_node {
    public:
        using edge_type = typename Adaptor::edge_type;
        using stream_t  = typename Adaptor::stream_t;

        struct dictionary_info {
            // How many characters back does the dictionary match begin at.
            size_t distance;
            // How long the dictionary match is.
            size_t length;
        };

        using symbolwise_info = std::span<stream_t const>;

        using symbolwise_data = std::vector<stream_t>;

    private:
        // The first character after the match ends.
        size_t position{0};
        // Cost, in bits, of "covering" all of the characters in the match.
        edge_type type;
        // Data about the match.
        std::variant<stream_t, dictionary_info, symbolwise_info, symbolwise_data> match{
                stream_t{0}};

    public:
        // Constructors.
        constexpr adj_list_node() noexcept : type(edge_type::invalid) {}

        explicit constexpr adj_list_node(edge_type type_in) noexcept : type(type_in) {}

        constexpr adj_list_node(
                size_t position_in, stream_t symbol, edge_type type_in) noexcept
                : position(position_in), type(type_in), match(symbol) {}

        constexpr adj_list_node(
                size_t position_in, symbolwise_info info, edge_type type_in) noexcept
                : position(position_in), type(type_in), match(info) {}

        constexpr adj_list_node(
                size_t position_in, symbolwise_data info, edge_type type_in) noexcept
                : position(position_in), type(type_in), match(info) {}

        constexpr adj_list_node(
                size_t position_in, dictionary_info info, edge_type type_in) noexcept
                : position(position_in), type(type_in), match(info) {}

        // Getters.
        [[nodiscard]] constexpr size_t get_position() const noexcept {
            return position;
        }

        [[nodiscard]] constexpr size_t get_destination() const noexcept {
            return position + get_length();
        }

        [[nodiscard]] constexpr size_t get_weight() const noexcept {
            return Adaptor::edge_weight(type, get_length());
        }

        [[nodiscard]] constexpr size_t get_distance() const noexcept {
            auto* ptr = std::get_if<dictionary_info>(&match);
            return ptr != nullptr ? ptr->distance : 0;
        }

        [[nodiscard]] constexpr size_t get_length() const noexcept {
            if (auto* ptr = std::get_if<dictionary_info>(&match); ptr != nullptr) {
                return ptr->length;
            }
            if (auto* ptr = std::get_if<symbolwise_info>(&match); ptr != nullptr) {
                return ptr->size();
            }
            if (auto* ptr = std::get_if<symbolwise_data>(&match); ptr != nullptr) {
                return ptr->size();
            }
            return 1;
        }

        [[nodiscard]] constexpr symbolwise_info get_data() const noexcept {
            if (auto* ptr = std::get_if<symbolwise_info>(&match); ptr != nullptr) {
                return *ptr;
            }
            if (auto* ptr = std::get_if<symbolwise_data>(&match); ptr != nullptr) {
                return *ptr;
            }
            return {};
        }

        [[nodiscard]] constexpr stream_t get_symbol() const noexcept {
            auto* ptr = std::get_if<stream_t>(&match);
            return ptr != nullptr ? *ptr : std::numeric_limits<stream_t>::max();
        }

        [[nodiscard]] constexpr edge_type get_type() const noexcept {
            return type;
        }

        [[nodiscard]] constexpr bool is_valid() const noexcept {
            return get_type() != edge_type::invalid;
        }
    };

    template <typename Adaptor, typename node_container_t>
    requires requires(node_container_t nodes, Adaptor::adj_list_node edge) {
        { nodes.emplace_back(edge) };
    }
    constexpr bool terminate(node_container_t& nodes, size_t& output_size) noexcept {
        using edge_type = typename Adaptor::edge_type;
        nodes.emplace_back(edge_type::terminator);
        output_size += Adaptor::edge_size(nodes.back());
        return false;
    }

    template <typename Adaptor, typename node_container_t>
    requires requires(node_container_t nodes, Adaptor::adj_list_node edge) {
        { nodes.emplace_back(edge) };
    }
    constexpr bool terminate(node_container_t& nodes) noexcept {
        size_t output_size = 0;
        return terminate<Adaptor>(nodes, output_size);
    }

    template <typename Adaptor, typename node_container_t>
    requires requires(node_container_t nodes, Adaptor::adj_list_node edge) {
        { nodes.emplace_back(edge) };
    }
    constexpr bool dictionary_match(
            node_container_t& nodes, size_t& output_size,
            std::make_signed_t<size_t> position, size_t distance, size_t length,
            typename Adaptor::edge_type type) noexcept {
        using dictionary_info = typename Adaptor::adj_list_node::dictionary_info;
        nodes.emplace_back(position, dictionary_info{distance, length}, type);
        output_size += Adaptor::edge_size(nodes.back());
        return true;
    }

    template <typename Adaptor, typename node_container_t>
    requires requires(node_container_t nodes, Adaptor::adj_list_node edge) {
        { nodes.emplace_back(edge) };
    }
    constexpr bool dictionary_match(
            node_container_t& nodes, std::make_signed_t<size_t> position, size_t distance,
            size_t length, typename Adaptor::edge_type type) noexcept {
        size_t output_size = 0;
        return dictionary_match<Adaptor>(
                nodes, output_size, position, distance, length, type);
    }

    template <typename Adaptor, typename node_container_t>
    requires requires(node_container_t nodes, Adaptor::adj_list_node edge) {
        { nodes.emplace_back(edge) };
    }
    constexpr bool symbolwise_match(
            node_container_t& nodes, size_t& output_size,
            std::make_signed_t<size_t> position,
            typename Adaptor::stream_t value) noexcept {
        using edge_type = typename Adaptor::edge_type;
        nodes.emplace_back(position, value, edge_type::symbolwise);
        output_size += Adaptor::edge_size(nodes.back());
        return true;
    }

    template <typename Adaptor, typename node_container_t>
    requires requires(node_container_t nodes, Adaptor::adj_list_node edge) {
        { nodes.emplace_back(edge) };
    }
    constexpr bool symbolwise_match(
            node_container_t& nodes, std::make_signed_t<size_t> position,
            typename Adaptor::stream_t value) noexcept {
        size_t output_size = 0;
        return symbolwise_match<Adaptor>(nodes, output_size, position, value);
    }

    template <typename Adaptor, typename node_container_t, typename stream_t>
    requires requires(node_container_t nodes, Adaptor::adj_list_node edge) {
        { nodes.emplace_back(edge) };
    }
    constexpr bool symbolwise_data(
            node_container_t& nodes, size_t& output_size,
            std::make_signed_t<size_t> position, stream_t& source, size_t length,
            typename Adaptor::edge_type type) noexcept {
        nodes.emplace_back(position, source.read_from_bytes(length), type);
        output_size += Adaptor::edge_size(nodes.back());
        return true;
    }

    template <typename Adaptor, typename node_container_t, typename stream_t>
    requires requires(node_container_t nodes, Adaptor::adj_list_node edge) {
        { nodes.emplace_back(edge) };
    }
    constexpr bool symbolwise_data(
            node_container_t& nodes, std::make_signed_t<size_t> position,
            stream_t& source, size_t length, typename Adaptor::edge_type type) noexcept {
        size_t output_size = 0;
        return symbolwise_data<Adaptor>(
                nodes, output_size, position, source, length, type);
    }

    template <typename Adaptor, typename node_container_t, typename stream_t>
    requires requires(node_container_t nodes, Adaptor::adj_list_node edge) {
        { nodes.emplace_back(edge) };
    }
    constexpr bool symbolwise_info(
            node_container_t& nodes, size_t& output_size,
            std::make_signed_t<size_t> position, stream_t* pointer, size_t length,
            typename Adaptor::edge_type type) noexcept {
        using symbolwise_info = typename Adaptor::adj_list_node::symbolwise_info;
        static_assert(std::is_same_v<
                      symbolwise_info, decltype(unsafe_forge_span(pointer, length))>);
        nodes.emplace_back(position, unsafe_forge_span(pointer, length), type);
        output_size += Adaptor::edge_size(nodes.back());
        return true;
    }

    template <typename Adaptor, typename node_container_t, typename stream_t>
    requires requires(node_container_t nodes, Adaptor::adj_list_node edge) {
        { nodes.emplace_back(edge) };
    }
    constexpr bool symbolwise_info(
            node_container_t& nodes, std::make_signed_t<size_t> position,
            stream_t* pointer, size_t length, typename Adaptor::edge_type type) noexcept {
        size_t output_size = 0;
        return symbolwise_info<Adaptor>(
                nodes, output_size, position, pointer, length, type);
    }

    template <typename Adaptor>
    class sliding_window {
    public:
        using edge_type    = typename Adaptor::edge_type;
        using stream_t     = typename Adaptor::stream_t;
        using node_t       = adj_list_node<Adaptor>;
        using match_t      = typename node_t::dictionary_info;
        using match_vector = std::vector<node_t>;
        using data_t       = std::span<stream_t const>;

        // NOLINTBEGIN(bugprone-easily-swappable-parameters)
        constexpr sliding_window(
                data_t data_in, size_t const search_buffer_size_in,
                size_t const minimal_match_length_in,
                size_t const lookahead_buffer_length, edge_type const type_in) noexcept
                : data(data_in), search_buffer_size(search_buffer_size_in),
                  minimal_match_length(minimal_match_length_in),
                  upper_bound(std::min(lookahead_buffer_length + base_node, data.size())),
                  lower_bound(base_node - std::min(base_node, search_buffer_size)),
                  type(type_in) {}

        // NOLINTEND(bugprone-easily-swappable-parameters)

        [[nodiscard]] size_t get_data_size() const {
            return data.size();
        }

        [[nodiscard]] size_t get_search_buffer_size() const {
            return base_node - lower_bound;
        }

        [[nodiscard]] size_t get_lookahead_buffer_size() const {
            return upper_bound - base_node;
        }

        [[nodiscard]] size_t get_window_size() const {
            return upper_bound - lower_bound;
        }

        bool slide_window() noexcept {
            if (upper_bound != data.size()) {
                upper_bound++;
            }
            if (base_node != data.size()) {
                base_node++;
            }
            if (get_search_buffer_size() > search_buffer_size) {
                lower_bound++;
            }
            return get_lookahead_buffer_size() != 0;
        }

#ifdef __clang__
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif

        void find_matches(match_vector& matches) const noexcept {
            static_assert(
                    noexcept(Adaptor::edge_weight(edge_type(), size_t())),
                    "Adaptor::edge_weight() is not noexcept");
            if (find_extra_matches(matches)) {
                return;
            }
            // This is what we produce.
            matches.clear();
            // First node is special.
            if (get_search_buffer_size() == 0) {
                return;
            }
            // Find the longest match in the search buffer.
            size_t const lookahead_length = get_lookahead_buffer_size();
            // Best match information.
            size_t best_pos = 0;
            size_t best_len = 0;
            // This is what we are looking to match ("needle").
            auto const* const needle = data.data() + base_node;
            for (size_t match_base = base_node; match_base > lower_bound; --match_base) {
                // This is where we are looking for matches ("haystack").
                auto const* haystack = data.data() + match_base - 1;
                // Keep looking for dictionary matches.
                size_t match_length = 0;
                while (match_length < lookahead_length
                       && haystack[match_length] == needle[match_length]) {
                    ++match_length;
                }
                if (best_len < match_length) {
                    best_pos = match_base - 1;
                    best_len = match_length;
                }
                if (match_length == lookahead_length) {
                    break;
                }
            }

            if (best_len >= minimal_match_length) {
                // We have found a match that links (base_node) with
                // (base_node + best_len) with length (best_len) and distance
                // equal to (base_node-best_pos).
                // Add it, and all prefixes, to the list, as long as it is a better
                // match.
                for (size_t jj = minimal_match_length; jj <= best_len; ++jj) {
                    matches.emplace_back(
                            base_node, match_t{base_node - best_pos, jj}, type);
                }
            }
        }

#ifdef __clang__
#    pragma clang diagnostic pop
#endif

    private:
        [[nodiscard]] bool find_extra_matches(match_vector& matches) const noexcept {
            static_assert(
                    noexcept(Adaptor::extra_matches(
                            data, base_node, upper_bound, lower_bound,
                            std::declval<match_vector&>())),
                    "Adaptor::extra_matches() is not noexcept");
            // This is what we produce.
            matches.clear();
            // Get extra dictionary matches dependent on specific encoder.
            return Adaptor::extra_matches(
                    data, base_node, upper_bound, lower_bound, matches);
        }

        // Source file data and its size; one node per character in source file.
        data_t    data;
        size_t    search_buffer_size;
        size_t    minimal_match_length;
        size_t    base_node{Adaptor::first_match_position};
        size_t    upper_bound;
        size_t    lower_bound;
        edge_type type;
    };

    template <typename Adaptor>
    concept adaptor_stream = requires {
        requires std::unsigned_integral<typename Adaptor::descriptor_t>;
        requires !std::same_as<typename Adaptor::descriptor_t, bool>;
        requires std::is_class_v<typename Adaptor::descriptor_endian_t>;
        requires std::same_as<decltype(Adaptor::need_early_descriptor), bool const>;
    };

    /*
     * This class abstracts away an LZSS output stream composed of one or more bytes
     * in a descriptor bitfield, followed by byte parameters. It manages the output
     * by buffering the bytes until a descriptor field is full, at which point it
     * writes the descriptor field and flushes the output buffer.
     */
    template <adaptor_stream Adaptor>
    class ostream {
    private:
        using descriptor_t        = typename Adaptor::descriptor_t;
        using descriptor_endian_t = typename Adaptor::descriptor_endian_t;
        using stream_t            = typename Adaptor::stream_t;
        using bit_buffer_t        = obitstream<
                       descriptor_t, Adaptor::descriptor_bit_order, descriptor_endian_t>;
        // Where we will output to.
        std::ostream& out;
        // Internal bitstream output buffer.
        bit_buffer_t bits;
        // Internal parameter buffer.
        std::string buffer;

        void flush_buffer() noexcept {
            out.write(buffer.c_str(), static_cast<std::streamsize>(buffer.size()));
            buffer.clear();
        }

    public:
        // Constructor.
        explicit ostream(std::ostream& dest) noexcept : out(dest), bits(out) {}

        ostream(ostream const&)                = delete;
        ostream(ostream&&) noexcept            = delete;
        ostream& operator=(ostream const&)     = delete;
        ostream& operator=(ostream&&) noexcept = delete;

        // Destructor: writes anything that hasn't been written.
        ~ostream() noexcept {
            // We need a dummy descriptor field if we have exactly zero bits left
            // on the previous descriptor field; this is because the decoder will
            // immediately fetch a new descriptor field when the previous one has
            // expired, and we don't want it to be the terminating sequence.
            // First, save current state.
            bool const need_dummy_descriptor = !bits.have_waiting_bits();
            // Now, flush the queue if needed.
            bits.flush();
            if constexpr (Adaptor::need_early_descriptor) {
                if (need_dummy_descriptor) {
                    // We need to add a dummy descriptor field; so add it.
                    descriptor_endian_t::write(out, descriptor_t{0});
                }
            }
            // Now write the terminating sequence if it wasn't written already.
            flush_buffer();
        }

        // Writes a bit to the descriptor bitfield. When the descriptor field is
        // full, outputs it and the output parameter buffer.
        void descriptor_bit(descriptor_t const bit) noexcept {
            if constexpr (Adaptor::need_early_descriptor) {
                if (bits.push(bit)) {
                    flush_buffer();
                }
            } else {
                if (!bits.have_waiting_bits()) {
                    flush_buffer();
                }
                bits.push(bit);
            }
        }

        // Writes up to sizeof(T) * CHAR_BIT bits to the buffer. This remembers
        // previously written bits, and outputs a T to the actual buffer once
        // there are at least sizeof(T) * CHAR_BIT bits stored in the buffer.
        // If the buffer was filled, it is written out and the output parameter
        // buffer is written as well, before any left-over bits are written to
        // the internal buffer.
        void descriptor_bits(descriptor_t const value, size_t count) noexcept {
            if constexpr (Adaptor::need_early_descriptor) {
                if (bits.write(value, count)) {
                    flush_buffer();
                }
            } else {
                if (!bits.have_waiting_bits()) {
                    flush_buffer();
                }
                bits.write(value, count);
            }
        }

        // Puts a byte in the output buffer.
        void put_byte(size_t const value) noexcept {
            write1(buffer, static_cast<uint8_t>(value));
        }

        void write_as_bytes(std::span<stream_t const> data) {
            detail::write_as_bytes(buffer, data);
        }

        ostream& write(char* pointer, std::streamsize count) noexcept {
            out.write(pointer, count);
            return *this;
        }
    };

    /*
     * This class abstracts away an LZSS input stream composed of one or more bytes
     * in a descriptor bitfield, followed by byte parameters. It manages the input
     * by reading a descriptor field when one is required (as defined by the adaptor
     * class), so that bytes can be read when needed from the input stream.
     */
    template <adaptor_stream Adaptor>
    class istream {
    private:
        using descriptor_t        = typename Adaptor::descriptor_t;
        using descriptor_endian_t = typename Adaptor::descriptor_endian_t;
        using stream_t            = typename Adaptor::stream_t;
        using bit_buffer_t        = ibitstream<
                       descriptor_t, Adaptor::descriptor_bit_order, descriptor_endian_t,
                       Adaptor::need_early_descriptor>;
        // Where we will input to.
        std::istream* in;
        // Internal bitstream input buffer.
        bit_buffer_t bits;

    public:
        // Constructor.
        explicit istream(std::istream& source) noexcept : in(&source), bits(*in) {}

        // Reads a bit from the descriptor bitfield.
        constexpr descriptor_t descriptor_bit() noexcept {
            return bits.pop();
        }

        // Reads up to sizeof(T) * CHAR_BIT bits from the descriptor bitfield.
        constexpr descriptor_t descriptor_bits(size_t count) noexcept {
            return bits.read(count);
        }

        // Puts a byte in the input buffer.
        constexpr uint8_t get_byte() noexcept {
            return read1(*in);
        }

        [[nodiscard]] std::vector<stream_t> read_from_bytes(size_t count) {
            return detail::read_from_bytes<std::vector<stream_t>>(*in, count);
        }

        constexpr istream& read(char* pointer, std::streamsize count) noexcept {
            in->read(pointer, count);
            return *this;
        }

        [[nodiscard]] std::streampos tellg() const noexcept {
            return in->tellg();
        }

        [[nodiscard]] constexpr bool eof() const noexcept {
            return in->eof();
        }

        [[nodiscard]] constexpr bool peek_eof() const noexcept {
            return in->peek() == std::istream::traits_type::eof();
        }
    };

    template <typename T>
    concept adaptor_t = requires {
        requires std::unsigned_integral<typename T::stream_t>;
        requires !std::same_as<typename T::stream_t, bool>;
        requires std::is_class_v<typename T::stream_endian_t>;
        requires std::unsigned_integral<typename T::descriptor_t>;
        requires !std::same_as<typename T::descriptor_t, bool>;
        requires std::is_class_v<typename T::descriptor_endian_t>;
        requires std::is_enum_v<typename T::edge_type>;
        requires std::is_class_v<typename T::adj_list_node>;
        requires std::is_class_v<typename T::adj_list>;
        requires std::is_class_v<typename T::istream_t>;
        requires std::is_class_v<typename T::ostream_t>;
        { T::edge_type::invalid };
        { T::edge_type::terminator };
        { T::edge_type::symbolwise };
        requires std::same_as<decltype(T::num_desc_bits), size_t const>;
        requires std::same_as<decltype(T::need_early_descriptor), bool const>;
        requires std::same_as<decltype(T::descriptor_bit_order), bit_endian const>;
        requires std::same_as<decltype(T::first_match_position), size_t const>;
        requires std::same_as<decltype(T::search_buf_size), size_t const>;
        requires std::same_as<decltype(T::look_ahead_buf_size), size_t const>;
        requires requires(
                uint8_t const*& rptr, uint8_t*& wptr, std::istream& input,
                std::ostream& output, typename T::stream_t stream_val,
                typename T::stream_endian_t     stream_endian,
                typename T::descriptor_t        descriptor_val,
                typename T::descriptor_endian_t descriptor_endian) {
            {
                decltype(stream_endian)::template read<decltype(stream_val)>(rptr)
            } -> std::same_as<decltype(stream_val)>;
            {
                decltype(stream_endian)::template read<decltype(stream_val)>(input)
            } -> std::same_as<decltype(stream_val)>;
            { decltype(stream_endian)::write(wptr, stream_val) } -> std::same_as<void>;
            { decltype(stream_endian)::write(output, stream_val) } -> std::same_as<void>;
            {
                decltype(descriptor_endian)::template read<decltype(descriptor_val)>(rptr)
            } -> std::same_as<decltype(descriptor_val)>;
            {
                decltype(descriptor_endian)::template read<decltype(descriptor_val)>(
                        input)
            } -> std::same_as<decltype(descriptor_val)>;
            {
                decltype(descriptor_endian)::write(wptr, descriptor_val)
            } -> std::same_as<void>;
            {
                decltype(descriptor_endian)::write(output, descriptor_val)
            } -> std::same_as<void>;
        };
        requires requires(
                typename T::edge_type type, size_t value, size_t lbound,
                std::vector<adj_list_node<T>> vnodes, T::adj_list lnodes,
                std::span<typename T::stream_t const> data, typename T::ostream_t output,
                typename T::istream_t input, typename T::adj_list_node edge,
                std::iostream& dest, size_t& output_size) {
            { T::create_sliding_window(data) };
            { T::desc_bits(type) } -> std::same_as<size_t>;
            { T::edge_weight(type, value) } -> std::same_as<size_t>;
            { T::extra_matches(data, value, value, value, vnodes) } -> std::same_as<bool>;
            { T::get_padding(value) } -> std::same_as<size_t>;
            { T::encode_edge(output, edge) };
            { T::decode_edge(input, lnodes, output_size) } -> std::same_as<bool>;
            { T::output_edge(dest, edge) } -> std::same_as<size_t>;
            { T::edge_size(edge) } -> std::same_as<size_t>;
            noexcept(T::desc_bits(type));
            noexcept(T::edge_weight(type, value));
            noexcept(T::get_padding(value));
            noexcept(T::extra_matches(data, value, value, value, vnodes));
        };
    };

    /*
     * Function which creates a LZSS structure and finds the optimal parse.
     */

    template <typename AdjList>
    struct parse_result {
        AdjList parse_list;
        size_t  desc_size;
        size_t  file_size;
    };

    template <adaptor_t Adaptor>
    auto find_optimal_parse(std::span<char const> data_in, Adaptor adaptor) noexcept {
        ignore_unused_variable_warning(adaptor);
        using edge_type       = typename Adaptor::edge_type;
        using stream_t        = typename Adaptor::stream_t;
        using stream_endian_t = typename Adaptor::stream_endian_t;
        using node_t          = adj_list_node<Adaptor>;
        using adj_list        = std::list<node_t>;
        using match_vector    = std::vector<node_t>;
        using data_t          = std::span<stream_t const>;

        auto read_stream = [](data_t data, size_t offset) {
            return stream_endian_t::template read<stream_t>(data[offset]);
        };

        // TODO: we should be allocating a std::vector<stream_t const> to begin with.
        // For the time being, I am doing this for now to suppress the GCC warning.
        // This is fine in this case because I am manually over-aligning the memory
        // that gets passed in by using an aligned allocator for std::vector, see
        // basic_decoder::encode.
        auto const* unaligned_ptr = static_cast<void const*>(data_in.data());
        auto const* aligned_ptr   = std::assume_aligned<alignof(stream_t)>(unaligned_ptr);
        assert(aligned_ptr == unaligned_ptr);
        data_t const data = unsafe_forge_span(
                static_cast<stream_t const*>(aligned_ptr),
                data_in.size() / sizeof(stream_t));
        static_assert(
                noexcept(Adaptor::desc_bits(edge_type())),
                "Adaptor::desc_bits() is not noexcept");
        static_assert(
                noexcept(Adaptor::get_padding(0)),
                "Adaptor::get_padding() is not noexcept");
        utils::assume(data.size() >= Adaptor::first_match_position);
        size_t const num_nodes = data.size() - Adaptor::first_match_position;
        utils::assume(num_nodes <= std::numeric_limits<size_t>::max() - 1);
        // Auxiliary data structures:
        // * The parent of a node is the node that reaches that node with the
        //   lowest cost from the start of the file.
        std::vector<size_t> parent_nodes(num_nodes + 1);
        // * This is the edge used to go from the parent of a node to said node.
        std::vector<node_t> parent_edges(num_nodes + 1);
        // * This is the total cost to reach the edge. They start as high as
        //   possible for all nodes but the first, which starts at 0.
        std::vector<size_t> total_costs(
                num_nodes + 1, std::numeric_limits<size_t>::max());
        total_costs[0] = 0;
        // * And this is a vector that tallies up the amount of bits in
        //   the descriptor bitfield for the shortest path up to this node.
        //   After tallying up the ending node, the end-of-file marker may cause
        //   an additional dummy descriptor bitfield to be emitted; this vector
        //   is used to counteract that.
        std::vector<size_t> descriptor_costs(
                num_nodes + 1, std::numeric_limits<size_t>::max());
        descriptor_costs[0] = 0;

        // Extracting distance relax logic from the loop so it can be used more
        // often.
        auto relax = [last_node = data.size(), &total_costs, &descriptor_costs,
                      &parent_nodes, &parent_edges](
                             size_t index, size_t const base_descriptor_cost,
                             auto const& elem) noexcept {
            // Need destination ID and edge weight.
            size_t const next_node
                    = elem.get_destination() - Adaptor::first_match_position;
            size_t edge_weight = total_costs[index] + elem.get_weight();
            // Compute descriptor bits from using this edge.
            size_t descriptor_cost
                    = base_descriptor_cost + Adaptor::desc_bits(elem.get_type());
            if (next_node == last_node) {
                // This is the ending node. Add the descriptor bits for the
                // end-of-file marker.
                edge_weight += Adaptor::edge_weight(edge_type::terminator, 0);
                descriptor_cost += Adaptor::desc_bits(edge_type::terminator);
                // If the descriptor bitfield had exactly 0 bits left after
                // this, we may need to emit a new descriptor bitfield (the
                // full Adaptor::num_desc_bits bits). Otherwise, we need to
                // pads the last descriptor bitfield to full size.
                // This accomplishes both.
                size_t const descriptor_modulus
                        = descriptor_cost % Adaptor::num_desc_bits;
                if (descriptor_modulus != 0 || Adaptor::need_early_descriptor) {
                    edge_weight += (Adaptor::num_desc_bits - descriptor_modulus);
                    descriptor_cost += (Adaptor::num_desc_bits - descriptor_modulus);
                }
                // Compensate for the Adaptor's padding, if any.
                edge_weight += Adaptor::get_padding(edge_weight);
            }
            // Is the cost to reach the target node through this edge less
            // than the current cost?
            if (total_costs[next_node] > edge_weight) {
                // If so, update the data structures with new best edge.
                total_costs[next_node]      = edge_weight;
                parent_nodes[next_node]     = index;
                parent_edges[next_node]     = elem;
                descriptor_costs[next_node] = descriptor_cost;
            }
        };

        // Since the LZSS graph is a topologically-sorted DAG by construction,
        // computing the shortest distance is very quick and easy: just go
        // through the nodes in order and update the distances.
        auto         win_set = Adaptor::create_sliding_window(data);
        match_vector matches;
        matches.reserve(Adaptor::look_ahead_buf_size);
        for (size_t ii = 0; ii < num_nodes; ii++) {
            // Get remaining unused descriptor bits up to this node.
            size_t const base_descriptor_cost = descriptor_costs[ii];
            // Start with the literal/symbolwise encoding of the current node.
            {
                size_t const    offset = ii + Adaptor::first_match_position;
                stream_t const  value  = read_stream(data, offset);
                edge_type const type   = edge_type::symbolwise;
                relax(ii, base_descriptor_cost, node_t(offset, value, type));
            }
            auto const relax_elem
                    = [&relax, ii, base_descriptor_cost](auto const& elem) noexcept {
                          if (elem.is_valid()) {
                              relax(ii, base_descriptor_cost, elem);
                          }
                      };
            // Get the adjacency list for this node.
            for (auto& window : win_set) {
                window.find_matches(matches);
                std::ranges::for_each(matches, relax_elem);
                window.slide_window();
            }
        }

        // This is what we will produce.
        parse_result<adj_list> result{
                {node_t{edge_type::terminator}},
                descriptor_costs.back(),
                total_costs.back()};
        adj_list& parse_list = result.parse_list;
        for (size_t ii = num_nodes; ii != 0;) {
            // Insert the edge up front...
            parse_list.push_front(parent_edges[ii]);
            // ... and switch to parent node.
            ii = parent_nodes[ii];
        }

        // We are done: this is the optimal parsing of the input file, giving
        // us *the* best possible compressed file size.
        return result;
    }

    // NOLINTBEGIN(bugprone-easily-swappable-parameters)
    template <adaptor_t Adaptor>
    inline void copy(
            std::iostream& dest, std::make_signed_t<size_t> const distance,
            std::make_signed_t<size_t> const length) {
        constexpr static size_t const num_bytes   = sizeof(typename Adaptor::stream_t);
        constexpr static size_t const buffer_size = Adaptor::look_ahead_buf_size;

        using diff_t   = std::make_signed_t<size_t>;
        using stream_t = typename Adaptor::stream_t;
        using buffer_t = boost::container::static_vector<stream_t, buffer_size>;
        diff_t const byte_distance = distance * static_cast<diff_t>(num_bytes);
        diff_t const pointer       = dest.tellp();
        dest.seekg(pointer - byte_distance);

        buffer_t buffer;
        if (distance == 1) {
            buffer.resize(
                    static_cast<size_t>(length),
                    source_endian::template read<stream_t>(dest));
        } else {
            buffer.resize(
                    static_cast<size_t>(std::min(length, distance)),
                    boost::container::default_init_t{});
            detail::read_from_bytes(dest, std::span<stream_t>(buffer));

            if (length > distance) {
                buffer.resize(static_cast<size_t>(length));
                auto       count  = length - distance;
                auto const start  = std::ranges::cbegin(buffer);
                auto       output = std::ranges::begin(buffer) + distance;
                auto       copied = distance;
                while (count > copied) {
                    auto [it_in, it_out] = std::ranges::copy(start, output, output);
                    count -= copied;
                    copied *= 2;
                    output = it_out;
                }
                std::ranges::copy(start, start + count, output);
            }
        }

        dest.seekp(pointer);
        detail::write_as_bytes(dest, std::span<stream_t const>(buffer));
    }

    template <adaptor_t Adaptor>
    auto encode(
            std::ostream& dest, std::span<char const> data, Adaptor adaptor) noexcept {
        using ostream_t = Adaptor::ostream_t;

        // Compute optimal Comper parsing of input file.
        auto      list = find_optimal_parse(data, adaptor);
        ostream_t output(dest);

        // Go through each edge in the optimal path.
        for (auto const& edge : list.parse_list) {
            Adaptor::encode_edge(output, edge);
        }
    }

}    // namespace lzss

// NOLINTEND(bugprone-easily-swappable-parameters)

#endif    // LIB_LZSS_HH
