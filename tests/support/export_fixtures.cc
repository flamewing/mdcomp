#include "fixtures/known_answers.hh"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: mdcomp-fixture-export OUTPUT_DIRECTORY\n";
        return 1;
    }
    try {
        std::filesystem::path const root(argv[1]);
        std::filesystem::create_directories(root);
        std::ofstream index(root / "index.tsv");
        index.exceptions(std::ios::failbit | std::ios::badbit);
        for (auto const& fixture : codec_test::known_answers()) {
            auto write = [&](char const* extension, std::string const& bytes) {
                auto path = root / (fixture.id + extension);
                std::filesystem::create_directories(path.parent_path());
                std::ofstream output(path, std::ios::binary);
                output.exceptions(std::ios::failbit | std::ios::badbit);
                output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            };
            write(".compressed", fixture.compressed);
            write(".raw", fixture.expected);
            index << fixture.id << '\t' << fixture.codec << '\t';
            for (size_t i = 0; i < fixture.edges.size(); ++i) {
                if (i) {
                    index << ',';
                }
                index << fixture.edges[i];
            }
            index << '\n';
        }
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
