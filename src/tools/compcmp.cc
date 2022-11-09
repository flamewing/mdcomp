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

#include "mdcomp/comper.hh"

#include <getopt.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

static void usage(char* prog) {
    std::cerr << "Usage: " << prog
              << " [-c|--crunch|-x|--extract=[{pointer}]] [-m|--moduled] "
                 "{input_filename} "
                 "{output_filename}"
              << std::endl;
    std::cerr << std::endl;
    std::cerr << "\t-x,--extract\tExtract from {pointer} address in file." << std::endl;
    std::cerr << "\t-c,--crunch \tAssume input file is Comper-compressed and "
                 "recompress to output file."
              << std::endl
              << "\t            \tIf --crunch is in effect, a missing "
                 "output_filename means recompress"
              << std::endl
              << "\t            \tto input_filename." << std::endl;
    std::cerr << "\t-m,--moduled\tUse compression in modules of 4096 bytes." << std::endl;
}

int main(int argc, char* argv[]) {
    constexpr static std::array const long_options{
            option{"extract", optional_argument, nullptr, 'x'},
            option{"moduled",       no_argument, nullptr, 'm'},
            option{ "crunch",       no_argument, nullptr, 'c'},
            option{  nullptr,                 0, nullptr,   0}
    };

    bool   extract = false;
    bool   moduled = false;
    bool   crunch  = false;
    size_t pointer = 0;

    while (true) {
        int       option_index = 0;
        int const option_char
                = getopt_long(argc, argv, "x::mc", long_options.data(), &option_index);
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
        case 'm':
            moduled = true;
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
        comper::decode(input, buffer);
        input.close();
        buffer.seekg(0);

        std::fstream output(
                outfile,
                std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output.good()) {
            std::cerr << "Output file '" << argv[optind + 1] << "' could not be opened."
                      << std::endl
                      << std::endl;
            return 3;
        }
        if (moduled) {
            comper::moduled_encode(buffer, output);
        } else {
            comper::encode(buffer, output);
        }
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
            if (moduled) {
                comper::moduled_decode(input, output);
            } else {
                comper::decode(input, output);
            }
        } else {
            if (moduled) {
                comper::moduled_encode(input, output);
            } else {
                comper::encode(input, output);
            }
        }
    }

    return 0;
}
