#ifndef MDCOMP_TEST_WIRE_BUILDER_HH
#define MDCOMP_TEST_WIRE_BUILDER_HH
#include <cstddef>
#include <string>

namespace codec_test::wire {
    inline void byte(std::string& out, unsigned value) {
        out += static_cast<char>(value & 0xff);
    }

    inline void word(std::string& out, unsigned value) {
        byte(out, value >> 8);
        byte(out, value);
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
                : width(bits), low_bits(lsb_bits), low_bytes(lsb_bytes), early(prefetch) {
        }

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

}    // namespace codec_test::wire
#endif
