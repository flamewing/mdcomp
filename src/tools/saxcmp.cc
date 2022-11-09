/*
 * Copyright (C) Flamewing 2013-2015 <flamewing.sonic@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mdcomp/saxman.hh"

#include <getopt.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

static void usage(char* prog) {
    std::cerr << "Usage: " << prog
              << " [-s size|-S] [-c|--crunch|-x|--extract=[{pointer}]] "
                 "{input_filename} {output_filename}"
              << std::endl;
    std::cerr << std::endl;
    std::cerr << "\t-x,--extract\tExtract from {pointer} address in file." << std::endl;
    std::cerr << "\t-c,--crunch \tAssume input file is Saxman-compressed and "
                 "recompress to output file."
              << std::endl
              << "\t            \tIf --crunch is in effect, a missing "
                 "output_filename means recompress"
              << std::endl
              << "\t            \tto input_filename." << std::endl
              << "\t-s size     \tAssume input file does not have a file size and "
                 "use value given instead."
              << std::endl
              << "\t            \tOnly affects decompression." << std::endl
              << "\t-S          \tCauses the compressor to not output a file size. "
                 "Only affects compression."
              << std::endl
              << std::endl;
}

int main(int argc, char* argv[]) {
    constexpr static std::array const long_options{
            option{"extract", optional_argument, nullptr, 'x'},
            option{ "crunch",       no_argument, nullptr, 'c'},
            option{  nullptr,                 0, nullptr,   0}
    };

    bool   extract   = false;
    bool   crunch    = false;
    bool   with_size = true;
    size_t pointer   = 0;
    size_t size      = 0;

    while (true) {
        int       option_index = 0;
        int const option_char
                = getopt_long(argc, argv, "x::cs:S", long_options.data(), &option_index);
        if (option_char == -1) {
            break;
        }

        switch (option_char) {
        case 'x':
            extract = true;
            if (optarg != nullptr) {
                pointer = strtoul(optarg, nullptr, 0);
            }
            break;
        case 'c':
            crunch = true;
            break;
        case 's':
            if (optarg != nullptr) {
                size = strtoul(optarg, nullptr, 0);
            }
            if (size == 0) {
                std::cerr << "Error: specified size must be a positive number."
                          << std::endl
                          << std::endl;
                return 4;
            }
            break;
        case 'S':
            with_size = false;
            break;
        default:
            break;
        }
    }

    if ((!crunch && argc - optind < 2) || (crunch && argc - optind < 1)) {
        usage(argv[0]);
        return 1;
    }

    if (extract && crunch) {
        std::cerr << "Error: --extract and --crunch can't be used at the same time."
                  << std::endl
                  << std::endl;
        return 4;
    }

    char const* outfile = crunch && argc - optind < 2 ? argv[optind] : argv[optind + 1];

    std::ifstream input(argv[optind], std::ios::in | std::ios::binary);
    if (!input.good()) {
        std::cerr << "Input file '" << argv[optind] << "' could not be opened."
                  << std::endl
                  << std::endl;
        return 2;
    }

    if (crunch) {
        std::stringstream buffer(std::ios::in | std::ios::out | std::ios::binary);
        input.seekg(static_cast<std::streamsize>(pointer));
        saxman::decode(input, buffer, size);
        input.close();
        buffer.seekg(0);

        std::ofstream output(outfile, std::ios::out | std::ios::binary);
        if (!output.good()) {
            std::cerr << "Output file '" << argv[optind + 1] << "' could not be opened."
                      << std::endl
                      << std::endl;
            return 3;
        }
        saxman::encode(buffer, output, with_size);
    } else {
        std::fstream output(
                outfile,
                std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output.good()) {
            std::cerr << "Output file '" << argv[optind + 1] << "' could not be opened."
                      << std::endl
                      << std::endl;
            return 3;
        }

        if (extract) {
            input.seekg(static_cast<std::streamsize>(pointer));
            saxman::decode(input, output, size);
        } else {
            saxman::encode(input, output, with_size);
        }
    }

    return 0;
}
