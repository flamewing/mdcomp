#include "support/format_observer.hh"

#include <map>
#include <stdexcept>
#include <utility>

namespace {
    constexpr size_t limit = 1024 * 1024;

    void require(bool condition, char const* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    struct reader {
        std::string_view data;
        size_t           pos = 0;

        unsigned byte() {
            require(pos < data.size(), "observer: truncated input");
            return static_cast<unsigned char>(data[pos++]);
        }

        unsigned word() {
            auto const high = byte();
            return (high << 8) | byte();
        }
    };

    class descriptors {
        reader&                  source;
        codec_test::observation& result;
        unsigned                 width, left = 0, value = 0, loads = 0;
        bool                     low_bits, early;

        void load() {
            value = source.byte();
            if (width == 16) {
                auto const next = source.byte();
                value           = low_bits ? value | (next << 8) : (value << 8) | next;
            }
            left = width;
            if (loads++) {
                result.edges.insert("descriptor-reload");
            }
        }

    public:
        descriptors(
                reader& r, codec_test::observation& o, unsigned w, bool low,
                bool prefetch = false)
                : source(r), result(o), width(w), low_bits(low), early(prefetch) {}

        unsigned bit() {
            if (!left) {
                load();
            }
            auto const bit = low_bits ? value & 1 : (value >> (left - 1)) & 1;
            if (low_bits) {
                value >>= 1;
            }
            if (!--left && early) {
                load();
            }
            return bit;
        }
    };

    struct bits {
        reader&  source;
        unsigned value = 0, left = 0;

        unsigned get(unsigned count) {
            unsigned out = 0;
            while (count--) {
                if (!left) {
                    value = source.byte();
                    left  = 8;
                }
                out = (out << 1) | ((value >> --left) & 1);
            }
            return out;
        }
    };
}    // namespace

codec_test::observation codec_test::observe(
        std::string_view codec, std::string_view compressed, options const& settings) {
    reader      source{compressed};
    observation result;
    auto        edge = [&](std::string const& name) {
        result.edges.insert(name);
    };
    auto append = [&](unsigned value) {
        require(result.decoded.size() < limit, "observer: output limit");
        result.decoded += static_cast<char>(value & 255);
    };
    auto word = [&](unsigned value) {
        append(value >> 8);
        append(value);
    };
    auto literal = [&](unsigned bytes = 1) {
        edge("literal");
        while (bytes--) {
            append(source.byte());
        }
    };
    auto copy = [&](size_t distance, size_t length, size_t unit = 1) {
        require(distance && distance * unit <= result.decoded.size(),
                "observer: unavailable history");
        edge("dictionary");
        if (length > distance) {
            edge("overlap");
        }
        for (size_t i = 0; i < length * unit; ++i) {
            append(static_cast<unsigned char>(
                    result.decoded[result.decoded.size() - distance * unit]));
        }
    };

    if (codec == "comper" || codec == "comperx") {
        descriptors desc(source, result, 16, false);
        for (size_t tokens = 0;; ++tokens) {
            require(tokens < limit, "observer: token limit");
            if (!desc.bit()) {
                literal(2);
                continue;
            }
            auto const displacement = source.byte(), count = source.byte();
            if (!count) {
                edge("terminator");
                break;
            }
            size_t const distance = codec == "comper" ? 256 - displacement
                                    : displacement    ? 257 - displacement
                                                      : 1;
            size_t const length   = codec == "comper" ? count + 1
                                                      : ((256 - 2 * (count & 127)) & 255)
                                                                + (count >> 7);
            if (codec == "comperx" && !displacement) {
                edge("rle");
            }
            if (length == 2) {
                edge("minimum-length");
            }
            if (length == (codec == "comper" ? 256U : 255U)) {
                edge("maximum-length");
            }
            if (distance == 256) {
                edge("maximum-distance");
            }
            copy(distance, length, 2);
        }
    } else if (codec == "kosinski" || codec == "kosplus") {
        bool const  plus = codec == "kosplus";
        descriptors desc(source, result, plus ? 8 : 16, !plus, !plus);
        for (size_t tokens = 0;; ++tokens) {
            require(tokens < limit, "observer: token limit");
            if (desc.bit()) {
                literal();
                continue;
            }
            size_t distance, length;
            if (!desc.bit()) {
                edge("inline");
                if (plus) {
                    distance        = 256 - source.byte();
                    auto const high = desc.bit();
                    length          = 2 + 2 * high + desc.bit();
                } else {
                    auto const high = desc.bit();
                    length          = 2 + 2 * high + desc.bit();
                    distance        = 256 - source.byte();
                }
            } else {
                auto const first = source.byte(), second = source.byte();
                auto const high = plus ? first : second, low = plus ? second : first;
                distance = 8192 - (low | ((high & 248) << 5));
                length   = high & 7;
                if (length) {
                    edge("medium");
                    length = plus ? 10 - length : length + 2;
                } else {
                    auto const extension = source.byte();
                    if (!extension) {
                        edge("terminator");
                        break;
                    }
                    if (!plus && extension == 1) {
                        edge("discard-control");
                        continue;
                    }
                    edge("long");
                    length = extension + (plus ? 9 : 1);
                }
            }
            if (length == 2) {
                edge("minimum-length");
            }
            if (length == (plus ? 264U : 256U)) {
                edge("maximum-length");
            }
            if (distance == 8192) {
                edge("maximum-distance");
            }
            copy(distance, length);
        }
    } else if (codec == "lzkn1") {
        auto const  expected = source.word();
        descriptors desc(source, result, 8, true);
        for (size_t tokens = 0;; ++tokens) {
            require(tokens < limit, "observer: token limit");
            if (!desc.bit()) {
                literal();
                continue;
            }
            auto const value = source.byte();
            if (value == 31) {
                edge("terminator");
                break;
            }
            if (value >= 192) {
                edge("packed-literals");
                auto const length = value - 192 + 8;
                if (length == 8) {
                    edge("minimum-packed");
                }
                if (length == 71) {
                    edge("maximum-packed");
                }
                for (unsigned i = 0; i < length; ++i) {
                    append(source.byte());
                }
            } else {
                size_t distance, length;
                if (value & 128) {
                    edge("short");
                    distance = value & 15;
                    length   = (value >> 4) - 6;
                } else {
                    edge("long");
                    distance = ((value << 3) & 768) | source.byte();
                    length   = (value & 31) + 3;
                }
                if (length == 2) {
                    edge("minimum-length");
                }
                if (length == 34) {
                    edge("maximum-length");
                }
                if (distance == 1023) {
                    edge("maximum-distance");
                }
                copy(distance, length);
            }
        }
        require(result.decoded.size() == expected, "observer: LZKN1 size mismatch");
    } else if (codec == "saxman" || codec == "rocket") {
        bool const sax      = codec == "saxman";
        size_t     expected = 0, body;
        if (sax) {
            if (settings.size_header) {
                auto const low = source.byte();
                body           = low | (source.byte() << 8);
                edge("sized");
            } else {
                body = settings.compressed_size;
                edge("headerless");
            }
        } else {
            expected = source.word();
            body     = source.word();
        }
        require(body <= compressed.size() - source.pos, "observer: body exceeds input");
        auto const end = source.pos + body;
        source.data    = compressed.substr(0, end);
        descriptors desc(source, result, 8, true);
        while (source.pos < end) {
            auto const flag = desc.bit();
            if (source.pos == end) {
                break;    // A final descriptor can have no commands.
            }
            if (flag) {
                literal();
                continue;
            }
            auto const   first = source.byte(), second = source.byte();
            size_t const length = sax ? (second & 15) + 3 : (first >> 2) + 1;
            size_t const window = sax ? 4096 : 1024;
            size_t const offset
                    = sax ? first | ((second & 240) << 4) : ((first & 3) << 8) | second;
            size_t const bias = sax ? 4096 - 18 : 960;
            size_t const distance
                    = window
                      - ((offset + window - ((result.decoded.size() + bias) % window))
                         % window);
            if (length == (sax ? 3U : 1U)) {
                edge("minimum-length");
            }
            if (length == (sax ? 18U : 64U)) {
                edge("maximum-length");
            }
            if (distance == window) {
                edge("maximum-distance");
            }
            if (distance > result.decoded.size()) {
                edge("seed");
                size_t const missing = distance - result.decoded.size();
                if (!sax && length > missing) {
                    edge("seed-to-output");
                }
                for (size_t i = 0; i < length; ++i) {
                    if (sax || i < missing) {
                        append(sax ? 0 : 32);
                    } else {
                        append(static_cast<unsigned char>(
                                result.decoded[result.decoded.size() - distance]));
                    }
                }
            } else {
                copy(distance, length);
            }
        }
        edge("body-end");
        if (!sax) {
            require(result.decoded.size() == expected, "observer: Rocket size mismatch");
        }
    } else if (codec == "enigma") {
        auto const width = source.byte(), flags = source.byte();
        require(width <= 11 && flags <= 31, "observer: invalid Enigma header");
        auto       increment = source.word();
        auto const common    = source.word();
        bits       wire{source};
        // Flags are packed above the low tile bits, not necessarily contiguous.
        auto tile = [&] {
            unsigned value = 0;
            for (int flag = 4; flag >= 0; --flag) {
                if (flags & (1U << flag)) {
                    value |= wire.get(1) << (flag + 11);
                }
            }
            return value | wire.get(width);
        };
        if (flags) {
            edge("tile-flags");
        }
        for (size_t tokens = 0;; ++tokens) {
            require(tokens < limit, "observer: token limit");
            auto kind = wire.get(2);
            if (kind & 2) {
                kind = (kind << 1) | wire.get(1);
            }
            auto const count = wire.get(4) + 1;
            if (kind == 7 && count == 16) {
                edge("terminator");
                break;
            }
            edge("packet-" + std::to_string(kind));
            if (count == 1) {
                edge("minimum-packet");
            }
            if (count == (kind == 7 ? 15U : 16U)) {
                edge("maximum-packet");
            }
            unsigned value = kind == 0   ? increment
                             : kind == 1 ? common
                             : kind == 7 ? 0
                                         : tile();
            for (unsigned i = 0; i < count; ++i) {
                word(kind == 7 ? tile() : value);
                value += kind == 0 || kind == 5 ? 1 : kind == 6 ? unsigned(-1) : 0;
            }
            if (kind == 0) {
                increment = value & 65535;
            }
        }
    } else if (codec == "nemesis") {
        auto const   header  = source.word();
        size_t const nibbles = (header & 32767) * 64;
        require(nibbles / 2 <= limit, "observer: Nemesis output limit");
        edge(header & 32768 ? "xor" : "normal");
        if (nibbles) {
            std::map<std::pair<unsigned, unsigned>, std::pair<unsigned, unsigned>> table;
            unsigned palette = 0;
            while (true) {
                auto token = source.byte();
                if (token == 255) {
                    break;
                }
                if (token & 128) {
                    palette = token & 15;
                    token   = source.byte();
                }
                auto const length = token & 15, count = (token >> 4) + 1,
                           code = source.byte();
                require(length && length <= 8 && count <= 8 && code < (1U << length),
                        "observer: invalid Nemesis table");
                for (auto const& entry : table) {
                    auto const [other_length, other_code] = entry.first;
                    require(length < other_length
                                    ? other_code >> (other_length - length) != code
                                    : code >> (length - other_length) != other_code,
                            "observer: ambiguous Nemesis table");
                }
                require(length < 6 ? code != (1U << length) - 1
                                   : code >> (length - 6) != 63,
                        "observer: reserved Nemesis prefix");
                table[{length, code}] = {count, palette};
            }
            bits     wire{source};
            size_t   produced = 0;
            unsigned pending  = 0;
            while (produced < nibbles) {
                unsigned code = 0, count = 0, value = 0;
                for (unsigned length = 1; length <= 8; ++length) {
                    code = (code << 1) | wire.get(1);
                    if (length == 6 && code == 63) {
                        edge("inline");
                        count = wire.get(3) + 1;
                        value = wire.get(4);
                        break;
                    }
                    auto const found = table.find({length, code});
                    if (found != table.end()) {
                        edge("table");
                        count = found->second.first;
                        value = found->second.second;
                        break;
                    }
                }
                require(count && count <= nibbles - produced,
                        "observer: invalid Nemesis run");
                if (count == 1) {
                    edge("minimum-run");
                }
                if (count == 8) {
                    edge("maximum-run");
                }
                while (count--) {
                    pending = (pending << 4) | value;
                    if (++produced % 2 == 0) {
                        append(pending);
                    }
                }
            }
            if (header & 32768) {
                for (size_t i = 4; i < result.decoded.size(); ++i) {
                    result.decoded[i] ^= result.decoded[i - 4];
                }
            }
        }
    } else if (codec == "snkrle") {
        auto const expected = source.word();
        if (expected) {
            auto current = source.byte();
            append(current);
            edge("literal");
            while (result.decoded.size() < expected) {
                auto const next = source.byte();
                append(next);
                if (current != next) {
                    current = next;
                    edge("literal");
                    continue;
                }
                auto const count = source.byte();
                edge("run");
                require(count <= expected - result.decoded.size(),
                        "observer: RLE exceeds size");
                if (!count) {
                    edge("minimum-run");
                }
                if (count == 255) {
                    edge("maximum-run");
                }
                for (unsigned i = 0; i < count; ++i) {
                    append(next);
                }
                if (count == 255 && result.decoded.size() < expected) {
                    current = source.byte();
                    append(current);
                    edge("continuation");
                }
            }
        }
        edge("size-end");
    } else {
        throw std::runtime_error("observer: unsupported codec");
    }
    if (result.decoded.empty()) {
        edge("empty");
    }
    result.consumed = source.pos;
    return result;
}
