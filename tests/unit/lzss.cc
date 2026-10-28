#include "mdcomp/lzss.hh"

#include "support/rejecting_output.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <list>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
    template <typename Descriptor, bool Early>
    struct stream_options {
        using stream_t                                    = uint8_t;
        using descriptor_t                                = Descriptor;
        using descriptor_endian_t                         = big_endian;
        constexpr static bool       need_early_descriptor = Early;
        constexpr static bit_endian descriptor_bit_order  = bit_endian::big;
    };

    template <typename Descriptor = uint8_t, bool Early = false, size_t Prefix = 0>
    struct adaptor {
        enum class edge_type {
            invalid,
            terminator,
            symbolwise,
            dictionary
        };
        using stream_t                                    = uint8_t;
        using stream_endian_t                             = big_endian;
        using descriptor_t                                = Descriptor;
        using descriptor_endian_t                         = big_endian;
        using adj_list_node                               = lzss::adj_list_node<adaptor>;
        using adj_list                                    = std::list<adj_list_node>;
        constexpr static bool       need_early_descriptor = Early;
        constexpr static bit_endian descriptor_bit_order  = bit_endian::big;
        constexpr static size_t     first_match_position  = Prefix;
        constexpr static size_t     search_buf_size       = 8;
        constexpr static size_t     look_ahead_buf_size   = 8;
        constexpr static size_t     num_desc_bits         = sizeof(Descriptor) * 8;
        using istream_t = lzss::istream<stream_options<Descriptor, Early>>;
        using ostream_t = lzss::ostream<stream_options<Descriptor, Early>>;

        constexpr static size_t desc_bits(edge_type) noexcept {
            return 1;
        }

        constexpr static size_t edge_weight(edge_type type, size_t) noexcept {
            return type == edge_type::terminator ? 1 : 9;
        }

        constexpr static size_t edge_size(adj_list_node const& node) noexcept {
            return node.get_type() == edge_type::terminator ? 0 : node.get_length();
        }

        static auto create_sliding_window(std::span<uint8_t const>) {
            return std::array<lzss::sliding_window<adaptor>, 0>{};
        }

        static bool extra_matches(
                std::span<uint8_t const>, size_t, size_t, size_t,
                std::vector<adj_list_node>&) noexcept {
            return false;
        }

        static void encode_edge(ostream_t& output, adj_list_node const& node) {
            output.descriptor_bit(node.get_type() == edge_type::terminator);
            if (node.get_type() == edge_type::symbolwise) {
                output.put_byte(node.get_symbol());
            }
        }

        static bool decode_edge(istream_t&, adj_list&, size_t&) {
            return false;
        }

        static size_t output_edge(std::iostream&, adj_list_node const&) {
            return 0;
        }
    };

    static_assert(lzss::adaptor_t<adaptor<>>);

    struct supplemental_adaptor : adaptor<> {
        using adj_list_node = lzss::adj_list_node<supplemental_adaptor>;

        static bool extra_matches(
                std::span<uint8_t const> data, size_t position, size_t upper, size_t,
                std::vector<adj_list_node>& matches) noexcept {
            matches.emplace_back(
                    position, data.subspan(position, upper - position),
                    edge_type::symbolwise);
            return false;    // Retain this edge and also search the dictionary.
        }
    };

    struct throwing_nodes {
        using node = adaptor<>::adj_list_node;
        node value;

        template <typename... Args>
        node& emplace_back(Args&&...) {
            throw std::runtime_error("allocation failure");
        }

        node& back() {
            return value;
        }
    };
}    // namespace

TEST_CASE("LZSS retains supplemental edges alongside dictionary matches", "[lzss]") {
    std::array<uint8_t, 3> const               data{7, 7, 7};
    lzss::sliding_window<supplemental_adaptor> window(
            data, 8, 2, 3, adaptor<>::edge_type::dictionary);
    std::vector<supplemental_adaptor::adj_list_node> matches;
    window.find_matches(matches);
    REQUIRE(matches.size() == 1);
    CHECK(matches[0].get_length() == 3);
    CHECK(matches[0].get_data().data() == data.data());
    REQUIRE(window.slide_window());
    window.find_matches(matches);
    REQUIRE(matches.size() == 2);
    CHECK(matches[0].get_position() == 1);
    CHECK(matches[0].get_data().data() == data.data() + 1);
    CHECK(matches[1].get_type() == adaptor<>::edge_type::dictionary);
    CHECK(matches[1].get_distance() == 1);
    CHECK(matches[1].get_length() == 2);
    window.find_matches(matches);
    CHECK(matches.size() == 2);    // Previous-call results must still be cleared.
}

TEST_CASE("LZSS owns packed literals without a second allocation", "[lzss]") {
    using node = adaptor<>::adj_list_node;
    std::vector<uint8_t> bytes{1, 2, 3};
    auto*                original = bytes.data();
    node                 edge(7, std::move(bytes), adaptor<>::edge_type::symbolwise);
    CHECK(edge.get_data().data() == original);
    CHECK(edge.get_length() == 3);
    CHECK(edge.get_destination() == 10);
    throwing_nodes nodes;
    size_t         size = 0;
    CHECK_THROWS_AS(lzss::terminate<adaptor<>>(nodes, size), std::runtime_error);
    CHECK(size == 0);
    CHECK_THROWS_AS(
            lzss::symbolwise_match<adaptor<>>(nodes, size, 0, uint8_t{1}),
            std::runtime_error);
}

TEST_CASE("LZSS descriptor groups preserve parameter order across boundaries", "[lzss]") {
    auto check = []<bool Early>() {
        std::ostringstream                     destination;
        lzss::ostream<adaptor<uint8_t, Early>> output(destination);
        output.descriptor_bits(0x7f, 7);
        output.put_byte(0xa5);
        output.descriptor_bits(2, 2);
        output.finish();
        output.finish();
        CHECK(destination.str() == std::string("\xff\xa5\0", 3));
    };
    check.template operator()<false>();
    check.template operator()<true>();

    std::ostringstream       destination;
    lzss::ostream<adaptor<>> output(destination);
    output.descriptor_bit(1);
    big_endian::write2(output, 0x1234);
    std::array<uint8_t, 2> bytes{0x56, 0x78};
    output.write_as_bytes(bytes);
    output.finish();
    CHECK(destination.str() == std::string("\x80\x12\x34\x56\x78", 5));
}

TEST_CASE(
        "LZSS costs include descriptor padding terminators and history prefixes",
        "[lzss]") {
    for (size_t count : {size_t{0}, size_t{1}, size_t{2}, size_t{15}, size_t{16}}) {
        std::string bytes(count, 'x');
        auto        result = lzss::find_optimal_parse(
                std::span<char const>(bytes), adaptor<uint16_t>{});
        auto const descriptor_size = utils::round_up(count + 1, size_t{16});
        CHECK(result.desc_size == descriptor_size);
        CHECK(result.file_size == 8 * count + descriptor_size);
    }
    std::string bytes(9, 'x');
    auto        prefixed = lzss::find_optimal_parse(
            std::span<char const>(bytes), adaptor<uint8_t, false, 3>{});
    CHECK(prefixed.file_size == 6 * 8 + 8);
    std::string exact(7, 'x');
    auto        early = lzss::find_optimal_parse(
            std::span<char const>(exact), adaptor<uint8_t, true>{});
    CHECK(early.file_size == 7 * 8 + 16);
}

TEST_CASE("LZSS fill handles lengths beyond its fixed scratch buffer", "[lzss]") {
    std::stringstream destination;
    lzss::fill<adaptor<>>(destination, 0x20, 960);
    CHECK(destination.str() == std::string(960, ' '));
    CHECK_THROWS_AS(lzss::fill<adaptor<>>(destination, 0, -1), std::invalid_argument);
    std::stringstream history("abc", std::ios::in | std::ios::out);
    history.seekp(0, std::ios::end);
    lzss::copy<adaptor<>>(history, 3, 8);
    CHECK(history.str() == "abcabcabcab");
    CHECK_THROWS_AS(lzss::copy<adaptor<>>(history, 0, 1), std::out_of_range);
    CHECK_THROWS_AS(lzss::copy<adaptor<>>(history, 100, 1), std::out_of_range);
    CHECK_THROWS_AS(lzss::copy<adaptor<>>(history, 1, -1), std::out_of_range);
}

TEST_CASE(
        "LZSS stream wrappers propagate failures through explicit finalization",
        "[lzss]") {
    std::istringstream source;
    source.exceptions(std::ios::failbit);
    CHECK_THROWS_AS(lzss::istream<adaptor<>>(source), std::ios_base::failure);
    test_support::rejecting_output_buffer buffer;
    std::ostream                          destination(&buffer);
    destination.exceptions(std::ios::badbit);
    lzss::ostream<adaptor<>> output(destination);
    output.descriptor_bit(1);
    CHECK_THROWS_AS(output.finish(), std::ios_base::failure);
    CHECK(destination.bad());
}
