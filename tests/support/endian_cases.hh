#ifndef MDCOMP_TEST_ENDIAN_CASES_HH
#define MDCOMP_TEST_ENDIAN_CASES_HH

#include "mdcomp/bigendian_io.hh"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace test_support {
    template <typename Endian, typename T>
    constexpr bool fixed_io(T value, std::array<unsigned char, sizeof(T)> expected) {
        std::array<unsigned char, sizeof(T) + 2> buffer{};
        buffer.front() = 0xa5;
        buffer.back()  = 0x5a;
        auto* output   = buffer.data() + 1;
        Endian::write(output, value);
        if (output != buffer.data() + sizeof(T) + 1 || buffer.front() != 0xa5
            || buffer.back() != 0x5a) {
            return false;
        }
        for (size_t i = 0; i < sizeof(T); ++i) {
            if (buffer[i + 1] != expected[i]) {
                return false;
            }
        }
        auto const* input = buffer.data() + 1;
        if (Endian::template read<T>(input) != value || input != output) {
            return false;
        }
        T scalar{};
        Endian::write(scalar, value);
        if (scalar != std::bit_cast<T>(expected)
            || Endian::template read<T>(scalar) != value) {
            return false;
        }

        // Consume the entire range: the advanced reverse iterator equals rend().
        std::array<unsigned char, sizeof(T)> reverse_buffer{};
        auto                                 reverse_output = reverse_buffer.rbegin();
        Endian::write(reverse_output, value);
        if (reverse_output != reverse_buffer.rend()) {
            return false;
        }
        for (size_t i = 0; i < sizeof(T); ++i) {
            if (reverse_buffer[sizeof(T) - i - 1] != expected[i]) {
                return false;
            }
        }
        auto reverse_input = reverse_buffer.crbegin();
        return Endian::template read<T>(reverse_input) == value
               && reverse_input == reverse_buffer.crend();
    }

    constexpr bool fixed_io_cases() {
        return fixed_io<big_endian>(uint8_t{0x80}, {0x80})
               && fixed_io<little_endian>(uint8_t{0xff}, {0xff})
               && fixed_io<big_endian>(uint16_t{0x80ff}, {0x80, 0xff})
               && fixed_io<little_endian>(uint16_t{0x80ff}, {0xff, 0x80})
               && fixed_io<big_endian>(uint32_t{0x123480ff}, {0x12, 0x34, 0x80, 0xff})
               && fixed_io<little_endian>(uint32_t{0x123480ff}, {0xff, 0x80, 0x34, 0x12})
               && fixed_io<big_endian>(
                       uint64_t{0x0123456789abcdefULL},
                       {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef})
               && fixed_io<little_endian>(
                       uint64_t{0x0123456789abcdefULL},
                       {0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01});
    }
}    // namespace test_support

#endif
