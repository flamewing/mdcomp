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

    inline std::vector<fixture> const& external_module_blocks() {
        auto hex = [](std::string_view value) {
            std::string result;
            for (size_t i = 0; i < value.size(); i += 2) {
                result += static_cast<char>(
                        std::stoul(std::string(value.substr(i, 2)), nullptr, 16));
            }
            return result;
        };
        auto input = [](std::string_view recipe, size_t size) {
            std::string result(size, 'A');
            if (recipe == "ascending") {
                for (size_t i = 0; i < size; ++i) {
                    result[i] = static_cast<char>(i & 255);
                }
            }
            return result;
        };
        static std::vector<fixture> const fixtures{
#include "fixtures/external_modules.inc"
        };
        return fixtures;
    }
}    // namespace codec_test
#endif
