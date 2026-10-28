#ifndef MDCOMP_TEST_REJECTING_OUTPUT_HH
#define MDCOMP_TEST_REJECTING_OUTPUT_HH

#include <streambuf>

namespace test_support {
    class rejecting_output_buffer : public std::streambuf {
        std::streamsize xsputn(char const*, std::streamsize) override {
            return 0;
        }

        int_type overflow(int_type) override {
            return traits_type::eof();
        }
    };
}    // namespace test_support

#endif
