#ifndef MDCOMP_TEST_EXTERNAL_ANSWERS_HH
#define MDCOMP_TEST_EXTERNAL_ANSWERS_HH
#include "fixtures/known_answers.hh"

namespace codec_test {
    inline std::vector<fixture> const& external_answers() {
        auto hex = [](std::string_view value) {
            std::string result;
            for (size_t i = 0; i < value.size(); i += 2) {
                result += static_cast<char>(
                        std::stoul(std::string(value.substr(i, 2)), nullptr, 16));
            }
            return result;
        };
        static std::vector<fixture> const fixtures{
#include "fixtures/external.inc"
        };
        return fixtures;
    }
}    // namespace codec_test
#endif
