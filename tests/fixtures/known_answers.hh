#ifndef MDCOMP_TEST_KNOWN_ANSWERS_HH
#define MDCOMP_TEST_KNOWN_ANSWERS_HH

#include "support/codec_api.hh"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace codec_test {
    struct fixture {
        std::string                   id;
        std::string_view              codec;
        std::string                   compressed;
        std::string                   expected;
        std::vector<std::string_view> edges;
        options                       settings;
        std::optional<size_t>         consumed;
    };

    std::vector<fixture> const& known_answers();
}    // namespace codec_test
#endif
