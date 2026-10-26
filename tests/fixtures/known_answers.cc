#include "known_answers.hh"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <utility>

namespace codec_test {
    namespace {
        void byte(std::string& out, unsigned value) {
            out += static_cast<char>(value & 0xff);
        }

        void word(std::string& out, unsigned value) {
            byte(out, value >> 8);
            byte(out, value);
        }

        std::string bytes(std::initializer_list<unsigned> values) {
            std::string result;
            for (auto value : values) {
                byte(result, value);
            }
            return result;
        }

        std::string words(size_t count, unsigned value) {
            std::string result;
            while (count--) {
                word(result, value);
            }
            return result;
        }

        // Test-only wire assembly. No production endian, bitstream, or encoder
        // helper is used. Descriptor placeholders are patched in event order.
        class descriptors {
            unsigned width;
            bool     low_bits, low_bytes, early;
            size_t   start = 0;
            unsigned used = 0, value = 0;
            bool     active = false;

            void begin() {
                start = data.size();
                data.append(width / 8, '\0');
                used = value = 0;
                active       = true;
            }

        public:
            std::string data;

            descriptors(unsigned bits, bool lsb_bits, bool lsb_bytes, bool prefetch)
                    : width(bits), low_bits(lsb_bits), low_bytes(lsb_bytes),
                      early(prefetch) {}

            void bit(unsigned input) {
                if (!active || used == width) {
                    begin();
                }
                value |= (input & 1) << (low_bits ? used : width - used - 1);
                for (unsigned i = 0; i < width / 8; ++i) {
                    unsigned shift  = low_bytes ? 8 * i : width - 8 * (i + 1);
                    data[start + i] = static_cast<char>((value >> shift) & 0xff);
                }
                if (++used == width && early) {
                    begin();
                }
            }

            void bits(unsigned value_in, unsigned count) {
                while (count) {
                    bit(value_in >> --count);
                }
            }

            void put(unsigned value_in) {
                byte(data, value_in);
            }

            void put_word(unsigned value_in) {
                word(data, value_in);
            }
        };

        class bit_sequence {
            unsigned used = 0;

        public:
            std::string data;

            void bits(unsigned value, unsigned count) {
                while (count) {
                    if (used % 8 == 0) {
                        data += '\0';
                    }
                    auto current = static_cast<unsigned char>(data.back());
                    current |= static_cast<unsigned char>(
                            ((value >> --count) & 1) << (7 - used % 8));
                    data.back() = static_cast<char>(current);
                    ++used;
                }
            }

            std::string finish(unsigned word_bytes) const {
                auto result = data;
                while (result.size() % word_bytes) {
                    result += '\0';
                }
                // Read-ahead guard for early-refilling bit readers.
                result.append(word_bytes, '\0');
                return result;
            }
        };

        void comper_vectors(std::vector<fixture>& out, bool x) {
            std::string_view codec = x ? "comperx" : "comper";
            for (unsigned count : {1U, 15U, 16U, 17U}) {
                descriptors wire(16, false, false, false);
                std::string expected;
                for (unsigned i = 0; i < count; ++i) {
                    wire.bit(0);
                    wire.put_word(0x3000 + i);
                    word(expected, 0x3000 + i);
                }
                wire.bit(1);
                wire.put(0xff);
                wire.put(0);
                out.push_back({
                        std::string(codec) + "/literal-" + std::to_string(count),
                        codec,
                        wire.data,
                        expected,
                        {"literal", "descriptor-reload", "terminator"}
                });
            }
            std::vector<std::pair<unsigned, unsigned>> const lengths = x
                    ? std::vector<std::pair<unsigned, unsigned>>{{2, 0x7f}, {3, 0xff}, {254, 1}, {255, 0x81}}
                    : std::vector<std::pair<unsigned, unsigned>>{{2, 1}, {256, 0xff}};
            for (auto [length, code] : lengths) {
                descriptors wire(16, false, false, false);
                wire.bit(0);
                wire.put_word(0xabcd);
                wire.bit(1);
                wire.put(x ? 0 : 0xff);
                wire.put(code);
                wire.bit(1);
                wire.put(0xff);
                wire.put(0);
                out.push_back({
                        std::string(codec) + "/overlap-" + std::to_string(length),
                        codec,
                        wire.data,
                        words(length + 1, 0xabcd),
                        {x ? "rle" : "dictionary", "overlap", "length-boundary",
                                              "terminator"}
                });
            }
            descriptors wire(16, false, false, false);
            std::string expected;
            for (unsigned i = 0; i < 256; ++i) {
                wire.bit(0);
                wire.put_word(i);
                word(expected, i);
            }
            wire.bit(1);
            wire.put(x ? 1 : 0);
            wire.put(x ? 0x7f : 1);
            wire.bit(1);
            wire.put(0xff);
            wire.put(0);
            expected += bytes({0, 0, 0, 1});
            out.push_back({
                    std::string(codec) + "/distance-256",
                    codec,
                    wire.data,
                    expected,
                    {"dictionary", "maximum-distance", "descriptor-reload",
                      "terminator"}
            });
        }

        void kos_vectors(std::vector<fixture>& out, bool plus) {
            std::string_view codec = plus ? "kosplus" : "kosinski";
            auto             make  = [&] {
                return descriptors(plus ? 8 : 16, !plus, true, !plus);
            };
            auto terminate = [&](descriptors& wire) {
                wire.bits(1, 2);
                wire.put(0);
                wire.put(0);
                wire.put(0);
            };
            for (unsigned count : {1U, 7U, 8U, 15U, 16U, 17U, 32U}) {
                auto        wire = make();
                std::string expected;
                for (unsigned i = 0; i < count; ++i) {
                    wire.bit(1);
                    wire.put(i);
                    byte(expected, i);
                }
                terminate(wire);
                out.push_back({
                        std::string(codec) + "/literal-" + std::to_string(count),
                        codec,
                        wire.data,
                        expected,
                        {"literal", "descriptor-reload", "terminator"}
                });
            }
            for (unsigned kind = 0; kind < 3; ++kind) {
                for (unsigned length :
                     (kind == 0   ? std::array{2U, 5U}
                      : kind == 1 ? std::array{3U, 9U}
                      : plus      ? std::array{10U, 264U}
                                  : std::array{3U, 256U})) {
                    auto wire = make();
                    wire.bit(1);
                    wire.put('A');
                    wire.bits(kind == 0 ? 0 : 1, 2);
                    if (kind == 0) {
                        if (plus) {
                            wire.put(0xff);
                        }
                        wire.bits(length - 2, 2);
                        if (!plus) {
                            wire.put(0xff);
                        }
                    } else {
                        unsigned high
                                = 0xf8
                                  | (kind == 1 ? (plus ? 10 - length : length - 2) : 0);
                        wire.put(plus ? high : 0xff);
                        wire.put(plus ? 0xff : high);
                        if (kind == 2) {
                            wire.put(plus ? length - 9 : length - 1);
                        }
                    }
                    terminate(wire);
                    out.push_back({
                            std::string(codec) + "/match-" + std::to_string(kind) + "-"
                                    + std::to_string(length),
                            codec,
                            wire.data,
                            std::string(length + 1, 'A'),
                            {kind == 0   ? "inline-match"
                             : kind == 1 ? "short-match"
                                         : "long-match",
                                                       "overlap", "length-boundary", "terminator"}
                    });
                }
            }
            if (!plus) {
                auto wire = make();
                wire.bits(1, 2);
                wire.put(0);
                wire.put(0);
                wire.put(1);
                wire.bit(1);
                wire.put('B');
                terminate(wire);
                out.push_back({
                        "kosinski/discard-control",
                        codec,
                        wire.data,
                        "B",
                        {"discard-control", "literal", "terminator"}
                });
            }
        }

        void lzkn_vectors(std::vector<fixture>& out) {
            for (unsigned length : {8U, 71U}) {
                descriptors wire(8, true, false, false);
                wire.bit(1);
                wire.put(0xc0 + length - 8);
                std::string expected;
                for (unsigned i = 0; i < length; ++i) {
                    wire.put(i);
                    byte(expected, i);
                }
                wire.bit(1);
                wire.put(0x1f);
                std::string header;
                word(header, length);
                out.push_back({
                        "lzkn1/packed-" + std::to_string(length),
                        "lzkn1",
                        header + wire.data,
                        expected,
                        {"packed-literal", "length-boundary", "terminator"}
                });
            }
            for (unsigned length : {2U, 5U, 3U}) {
                descriptors wire(8, true, false, false);
                wire.bit(0);
                wire.put('A');
                wire.bit(1);
                if (length == 3) {
                    wire.put(0);
                    wire.put(1);
                } else {
                    wire.put(((length + 6) << 4) | 1);
                }
                wire.bit(1);
                wire.put(0x1f);
                std::string header;
                word(header, length + 1);
                out.push_back({
                        "lzkn1/overlap-" + std::to_string(length),
                        "lzkn1",
                        header + wire.data,
                        std::string(length + 1, 'A'),
                        {length == 3 ? "long-match" : "short-match", "overlap",
                                                   "terminator"}
                });
            }
            descriptors wire(8, true, false, false);
            std::string expected;
            for (unsigned i = 0; i < 256; ++i) {
                wire.bit(0);
                wire.put(i);
                byte(expected, i);
            }
            wire.bit(1);
            wire.put(0x3f);
            wire.put(0);    // distance 256, length 34
            wire.bit(1);
            wire.put(0x1f);
            expected += expected.substr(0, 34);
            std::string header;
            word(header, static_cast<unsigned>(expected.size()));
            out.push_back({
                    "lzkn1/long-length-34",
                    "lzkn1",
                    header + wire.data,
                    expected,
                    {"long-match", "maximum-length", "descriptor-reload", "terminator"}
            });
        }

        void byte_lz_vectors(std::vector<fixture>& out, bool sax) {
            for (bool seed : {false, true}) {
                for (unsigned length :
                     (sax ? std::array{3U, 18U} : std::array{2U, 64U})) {
                    descriptors wire(8, true, true, false);
                    if (!seed) {
                        wire.bit(1);
                        wire.put('A');
                    }
                    wire.bit(0);
                    if (sax) {
                        wire.put(0xee);
                        wire.put(0xf0 | (length - 3));
                    } else {
                        unsigned offset = seed ? 0 : 0x3c0;
                        wire.put(((length - 1) << 2) | (offset >> 8));
                        wire.put(offset);
                    }
                    std::string expected(
                            length + (seed ? 0 : 1), seed ? (sax ? '\0' : ' ') : 'A');
                    std::string header;
                    if (sax) {
                        byte(header, static_cast<unsigned>(wire.data.size()));
                        byte(header, static_cast<unsigned>(wire.data.size() >> 8));
                    } else {
                        word(header, static_cast<unsigned>(expected.size()));
                        word(header, static_cast<unsigned>(wire.data.size()));
                    }
                    out.push_back({
                            std::string(sax ? "saxman" : "rocket")
                                    + (seed ? "/seed-" : "/overlap-")
                                    + std::to_string(length),
                            sax ? "saxman" : "rocket",
                            header + wire.data,
                            expected,
                            {seed ? "seeded-fill" : "dictionary", "overlap",
                              "length-boundary"}
                    });
                    if (sax) {
                        out.back().edges.push_back("size-bound");
                        if (!seed) {
                            out.back().edges.push_back("literal");
                        }
                        auto raw = out.back();
                        raw.id += "-headerless";
                        raw.compressed               = wire.data;
                        raw.settings.size_header     = false;
                        raw.settings.compressed_size = wire.data.size();
                        out.push_back(std::move(raw));
                    }
                }
            }
            if (!sax) {
                descriptors wire(8, true, true, false);
                wire.bit(0);
                wire.put(0xff);
                wire.put(0xbf);
                out.push_back({
                        "rocket/seed-to-output",
                        "rocket",
                        bytes({0, 64, 0, 3}
                        ) + wire.data,
                        std::string(64, ' '),
                        {"seeded-fill", "seed-to-output", "maximum-length"}
                });
            }
        }

        void enigma_vectors(std::vector<fixture>& out) {
            for (unsigned mode = 0; mode < 6; ++mode) {
                for (unsigned count : {1U, mode == 5 ? 15U : 16U}) {
                    bit_sequence wire;
                    if (mode < 2) {
                        wire.bits(mode, 2);
                    } else {
                        wire.bits(mode + 2, 3);
                    }
                    wire.bits(count - 1, 4);
                    unsigned start = mode == 4 ? 1 : 0x123;
                    if (mode >= 2 && mode < 5) {
                        wire.bits(start, 11);
                    }
                    if (mode == 5) {
                        for (unsigned i = 0; i < count; ++i) {
                            wire.bits(i, 11);
                        }
                    }
                    wire.bits(0x7f, 7);
                    std::string expected;
                    for (unsigned i = 0; i < count; ++i) {
                        unsigned value = mode == 0   ? 0xfffe + i
                                         : mode == 1 ? 0x4321
                                         : mode == 2 ? start
                                         : mode == 3 ? start + i
                                         : mode == 4 ? start - i
                                                     : i;
                        word(expected, value & 0xffff);
                    }
                    out.push_back({
                            "enigma/mode-" + std::to_string(mode) + "-"
                                    + std::to_string(count),
                            "enigma",
                            bytes({11, 0, 0xff, 0xfe, 0x43, 0x21}
                            ) + wire.finish(2),
                            expected,
                            {mode == 0   ? "incrementing"
                             : mode == 1 ? "common"
                             : mode == 2 ? "inline-repeat"
                             : mode == 3 ? "inline-increment"
                             : mode == 4 ? "inline-decrement"
                                         : "inline-literals",
                                   "count-boundary", "terminator"}
                    });
                }
            }
            bit_sequence flags;
            flags.bits(0x71, 7);
            flags.bits(0xf123, 16);
            flags.bits(0x8001, 16);
            flags.bits(0x7f, 7);
            out.push_back({
                    "enigma/all-flags",
                    "enigma",
                    bytes({11, 31, 0, 0, 0, 0}
                    ) + flags.finish(2),
                    bytes({0xf1, 0x23, 0x80, 1}
                    ),
                    {"inline-literals", "all-flags", "terminator"}
            });
        }

        void nemesis_vectors(std::vector<fixture>& out) {
            for (bool xor_mode : {false, true}) {
                bit_sequence wire;
                for (unsigned i = 0; i < 8; ++i) {
                    wire.bits(0x3f, 6);
                    wire.bits(7, 3);
                    wire.bits(0xa, 4);
                }
                std::string expected(32, static_cast<char>(0xaa));
                if (xor_mode) {
                    for (size_t i = 0; i < 32; ++i) {
                        if ((i / 4) % 2) {
                            expected[i] = 0;
                        }
                    }
                }
                out.push_back({
                        xor_mode ? "nemesis/inline-xor" : "nemesis/inline-normal",
                        "nemesis",
                        bytes({xor_mode ? 0x80U : 0U,                  1,0xff                                                                 }
                        ) + wire.finish(1),
                        expected,
                        {         "inline-rle", "run-length-eight",
                               xor_mode ? "progressive-xor" : "normal"}
                });
            }
            out.push_back({
                    "nemesis/table-code",
                    "nemesis",
                    bytes({0, 1, 0x8a, 0x71, 0, 0xff, 0, 0}
                    ),
                    std::string(32, static_cast<char>(0xaa)),
                    {"table-code", "run-length-eight", "normal"}
            });
            out.push_back(
                    {"nemesis/empty", "nemesis", bytes({0, 0}), "", {"empty"}, {}, 2});
            bit_sequence          varied;
            std::vector<unsigned> nibbles;
            unsigned              nibble = 0;
            for (unsigned count : {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 7U, 7U, 7U, 7U}) {
                varied.bits(0x3f, 6);
                varied.bits(count - 1, 3);
                varied.bits(nibble, 4);
                nibbles.insert(nibbles.end(), count, nibble++);
            }
            std::string expected;
            for (size_t i = 0; i < nibbles.size(); i += 2) {
                byte(expected, (nibbles[i] << 4) | nibbles[i + 1]);
            }
            out.push_back({
                    "nemesis/inline-counts-one-through-eight",
                    "nemesis",
                    bytes({0, 1, 0xff}
                    ) + varied.finish(1),
                    expected,
                    {"inline-rle", "run-length-one", "run-length-eight",
                           "nibble-boundary", "normal"}
            });
        }

        void snkrle_vectors(std::vector<fixture>& out) {
            out.push_back(
                    {"snkrle/empty", "snkrle", bytes({0, 0}), "", {"empty"}, {}, 2});
            out.push_back(
                    {"snkrle/literals",
                     "snkrle",
                     bytes({0, 3, 'A', 'B', 'C'}),
                     "ABC",
                     {"literal"},
                     {},
                     5});
            for (unsigned count : {2U, 3U, 257U}) {
                std::string header;
                word(header, count);
                out.push_back({
                        "snkrle/run-" + std::to_string(count),
                        "snkrle",
                        header + bytes({'A', 'A', count - 2}
                          ),
                        std::string(count, 'A'),
                        {"run", "count-boundary"},
                        {},
                        5
                });
            }
            out.push_back({
                    "snkrle/continuation",
                    "snkrle",
                    bytes({1, 2, 'A', 'A', 255, 'B'}
                    ),
                    std::string(257, 'A') + 'B',
                    {"run", "count-255-continuation", "literal"},
                    {},
                    6
            });
        }
    }    // namespace

    std::vector<fixture> const& known_answers() {
        static auto const values = [] {
            std::vector<fixture> out;
            comper_vectors(out, false);
            comper_vectors(out, true);
            kos_vectors(out, false);
            kos_vectors(out, true);
            lzkn_vectors(out);
            byte_lz_vectors(out, true);
            byte_lz_vectors(out, false);
            enigma_vectors(out);
            nemesis_vectors(out);
            snkrle_vectors(out);
            return out;
        }();
        return values;
    }
}    // namespace codec_test
