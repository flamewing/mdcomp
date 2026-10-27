#include "support/codec_api.hh"
#include "support/format_observer.hh"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
    codec_test::api const& api(std::string_view name) {
        for (auto const& codec : codec_test::codecs()) {
            if (codec.name == name) {
                return codec;
            }
        }
        throw std::runtime_error("Unknown boundary codec");
    }

    class bounded_output : public std::streambuf {
        std::vector<char> bytes;
        size_t            capacity;
        size_t            high = 0;

    public:
        explicit bounded_output(size_t available)
                : bytes(available + 2, char(0x5a)), capacity(available) {
            bytes[capacity + 1] = char(0xa5);
            setp(bytes.data(), bytes.data() + capacity);
        }

        bool guards_intact() const {
            return bytes[capacity] == char(0x5a) && bytes[capacity + 1] == char(0xa5);
        }

        std::string contents() const {
            return {bytes.data(), std::max(high, static_cast<size_t>(pptr() - pbase()))};
        }

        int_type overflow(int_type) override {
            return traits_type::eof();
        }

        pos_type seekoff(
                off_type offset, std::ios_base::seekdir direction,
                std::ios_base::openmode mode) override {
            if (!(mode & std::ios_base::out)) {
                return pos_type(off_type(-1));
            }
            high                = std::max(high, static_cast<size_t>(pptr() - pbase()));
            auto const base     = direction == std::ios_base::beg ? off_type(0)
                                  : direction == std::ios_base::cur
                                          ? off_type(pptr() - pbase())
                                          : off_type(high);
            auto const position = base + offset;
            if (position < 0 || position > epptr() - pbase()) {
                return pos_type(off_type(-1));
            }
            auto* const end = epptr();
            setp(bytes.data(), end);
            pbump(static_cast<int>(position));
            return pos_type(position);
        }

        pos_type seekpos(pos_type position, std::ios_base::openmode mode) override {
            return seekoff(off_type(position), std::ios_base::beg, mode);
        }
    };
}    // namespace

TEST_CASE(
        "Fixed-width codec headers reject unrepresentable encodings before output",
        "[codec][limits][negative]") {
    for (auto name : {"lzkn1", "snkrle", "rocket", "nemesis", "saxman"}) {
        auto const& codec = api(name);
        auto const  input
                = std::string_view(name) == "nemesis"
                          ? std::string(0x7fff * 32 + 1, '\0')
                          : codec_test::corpus(
                                    65536, std::string_view(name) == "saxman" ? 4 : 0);
        auto const encoded = codec_test::run(codec.encode, input, {}, 0, "keep");
        CAPTURE(std::string(codec.name), input.size(), encoded.diagnostic);
        CHECK_FALSE(encoded.success);
        CHECK((encoded.bytes == "keep"));
    }
    auto const compressed_overflow = codec_test::run(
            api("rocket").encode, codec_test::corpus(65535, 4), {}, 0, "keep");
    CHECK_FALSE(compressed_overflow.success);
    CHECK((compressed_overflow.bytes == "keep"));
}

TEST_CASE(
        "Largest representable raw sizes round-trip through independent wire models",
        "[codec][limits][slow]") {
    for (auto const& [name, size] : std::array<std::pair<std::string_view, size_t>, 4>{
                 {{"lzkn1", 65535},
                  {"snkrle", 65534},
                  {"rocket", 65535},
                  {"nemesis", 0x7fff * 32}}
    }) {
        auto const& codec   = api(name);
        auto const  input   = std::string(size, name == "rocket" ? ' ' : '\0');
        auto const  encoded = codec_test::run(codec.encode, input);
        auto const  decoded = encoded.success
                                      ? codec_test::run(codec.decode, encoded.bytes)
                                      : codec_test::result{};
        CAPTURE(std::string(name), size, encoded.diagnostic, decoded.diagnostic);
        if (!encoded.success || !decoded.success || decoded.bytes != input) {
            codec_test::save_failure(name, "largest-header", input, encoded, decoded, 0);
        }
        REQUIRE(encoded.success);
        REQUIRE(decoded.success);
        CHECK((decoded.bytes == input));
        auto const observed = codec_test::observe(name, encoded.bytes);
        CHECK((observed.decoded == input));
    }
    // Saxman's field bounds compressed bytes, not the decompressed length.
    auto const input   = std::string(65536, 'A');
    auto const encoded = codec_test::run(api("saxman").encode, input);
    REQUIRE(encoded.success);
    auto const decoded = codec_test::run(api("saxman").decode, encoded.bytes);
    REQUIRE(decoded.success);
    CHECK((decoded.bytes == input));
}

TEST_CASE(
        "Saxman headerless encoding permits an externally sized large body",
        "[codec][limits][saxman]") {
    auto const          input = codec_test::corpus(65536, 4);
    codec_test::options settings;
    settings.size_header = false;
    auto const encoded   = codec_test::run(api("saxman").encode, input, settings);
    REQUIRE(encoded.success);
    REQUIRE(encoded.bytes.size() > 65535);
    bool found = false;
    // Transport alignment can append one byte; retain the actual body bound.
    for (size_t size : {encoded.bytes.size(), encoded.bytes.size() - 1}) {
        settings.compressed_size = size;
        bool matches             = false;
        try {
            matches = codec_test::observe("saxman", encoded.bytes, settings).decoded
                      == input;
        } catch (std::runtime_error const&) {
            // The longer candidate can end inside a token after alignment.
        }
        if (matches) {
            auto const decoded
                    = codec_test::run(api("saxman").decode, encoded.bytes, settings);
            REQUIRE(decoded.success);
            CHECK((decoded.bytes == input));
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE(
        "Encoder callbacks propagate bounded output failures",
        "[codec][limits][negative]") {
    auto const input = codec_test::corpus(64, 4);
    for (auto const& codec : codec_test::codecs()) {
        for (bool modules : {false, true}) {
            auto const operation = modules ? codec.moduled_encode : codec.encode;
            if (!operation) {
                continue;
            }
            auto const valid = codec_test::run(operation, input);
            REQUIRE(valid.success);
            REQUIRE(valid.bytes.size() > 8);
            {
                std::istringstream source(input);
                bounded_output     buffer(valid.bytes.size());
                std::iostream      output(&buffer);
                REQUIRE(operation(source, output, {}));
                CHECK((buffer.contents() == valid.bytes));
                CHECK(buffer.guards_intact());
            }
            for (size_t capacity : {size_t(0), size_t(1), size_t(4), size_t(8)}) {
                CAPTURE(std::string(codec.name), modules, capacity);
                std::istringstream source(input);
                bounded_output     buffer(capacity);
                std::iostream      output(&buffer);
                bool               accepted = false, threw = false;
                try {
                    accepted = operation(source, output, {});
                } catch (std::ios_base::failure const&) {
                    threw = true;
                }
                CHECK_FALSE(accepted);
                CHECK((threw || output.fail()));
                CHECK(buffer.guards_intact());
            }
        }
    }
}

TEST_CASE(
        "Encoders honor source offsets and reject incomplete word input",
        "[codec][limits][contract]") {
    auto const raw = codec_test::corpus(64, 4);
    for (auto const& codec : codec_test::codecs()) {
        for (bool modules : {false, true}) {
            auto const operation = modules ? codec.moduled_encode : codec.encode;
            if (!operation) {
                continue;
            }
            auto const plain    = codec_test::run(operation, raw);
            auto const embedded = codec_test::run(operation, "PREFIX" + raw, {}, 6);
            CAPTURE(std::string(codec.name), modules, embedded.diagnostic);
            REQUIRE(plain.success);
            REQUIRE(embedded.success);
            CHECK((embedded.bytes == plain.bytes));
        }
    }
    for (auto const& raw_word : {std::string("A"), std::string("ABC")}) {
        auto const enigma
                = codec_test::run(api("enigma").encode, raw_word, {}, 0, "keep");
        CHECK_FALSE(enigma.success);
        CHECK((enigma.bytes == "keep"));
        for (auto name : {"comper", "comperx"}) {
            auto const module
                    = codec_test::run(api(name).moduled_encode, raw_word, {}, 0, "keep");
            CAPTURE(name, raw_word.size());
            CHECK_FALSE(module.success);
            CHECK((module.bytes == "keep"));
        }
    }
}
