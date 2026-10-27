#ifndef MDCOMP_TEST_FORMAT_OBSERVER_HH
#define MDCOMP_TEST_FORMAT_OBSERVER_HH

#include "support/codec_api.hh"

#include <set>
#include <string>
#include <string_view>

namespace codec_test {
    struct observation {
        std::string           decoded;
        std::set<std::string> edges;
        size_t                consumed = 0;
    };

    // Test-owned wire interpreter: no production endian, bitstream, adaptor,
    // or codec helpers. A malformed stream throws with a bounded diagnostic.
    observation observe(
            std::string_view codec, std::string_view compressed,
            options const& settings = {});
}    // namespace codec_test
#endif
