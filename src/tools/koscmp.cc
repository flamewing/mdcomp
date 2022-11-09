/*
 * Copyright (C) Flamewing 2011-2015 <flamewing.sonic@gmail.com>
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

#include "mdcomp/kosinski.hh"

#include <getopt.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

static void usage(char* prog) {
    std::cerr << "Usage: " << prog
              << " [-c|--crunch|-x|--extract=[{pointer}]] [-m|--moduled] "
                 "[-p|--padding=[{len}]] {input_filename} {output_filename}"
              << std::endl;
    std::cerr << std::endl;
    std::cerr << "\t-x,--extract\tExtract from {pointer} address in file." << std::endl;
    std::cerr << "\t-c,--crunch \tAssume input file is Kosinski-compressed and "
                 "recompress to output file."
              << std::endl
              << "\t            \tIf --crunch is in effect, a missing "
                 "output_filename means recompress"
              << std::endl
              << "\t            \tto input_filename. All parameters affect only the "
                 "output file, except"
              << std::endl
              << "\t            \tfor the -m parameter, which makes both input and "
                 "output files moduled"
              << std::endl
              << "\t            \t(but the optional module size affects only the "
                 "output file)."
              << std::endl;
    std::cerr << "\t-m,--moduled\tUse compression in modules of 4096 bytes." << std::endl;
    std::cerr << "\t-p|--padding\tFor moduled compression only. Changes internal "
                 "module padding to {len}."
              << std::endl
              << "\t            \tEach module will be padded to a multiple of the "
                 "given number; use 1 for"
              << std::endl
              << "\t            \tno padding. Must be a power of 2 (default: "
              << moduled_kosinski::MODULE_PADDING << ")." << std::endl
              << std::endl;
}

int main(int argc, char* argv[]) {
    constexpr static std::array const long_options{
            option{"extract", optional_argument, nullptr, 'x'},
            option{"moduled",       no_argument, nullptr, 'm'},
            option{ "crunch",       no_argument, nullptr, 'c'},
            option{"padding", required_argument, nullptr, 'p'},
            option{  nullptr,                 0, nullptr,   0}
    };

    bool   extract = false;
    bool   moduled = false;
    bool   crunch  = false;
    size_t pointer = 0ULL;
    size_t padding = moduled_kosinski::MODULE_PADDING;

    while (true) {
        int       option_index = 0;
        int const option_char  = getopt_long(
                argc, argv, "x::mcr:s:p:", long_options.data(), &option_index);
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
        case 'p':
            if (optarg != nullptr) {
                padding = strtoul(optarg, nullptr, 0);
            }
            if ((padding == 0U) || (padding & (padding - 1)) != 0) {
                padding = 16;
            }
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
        if (moduled) {
            kosinski::moduled_decode(input, buffer, padding);
        } else {
            kosinski::decode(input, buffer);
        }
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
            kosinski::moduled_encode(buffer, output, padding);
        } else {
            kosinski::encode(buffer, output);
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
                kosinski::moduled_decode(input, output, padding);
            } else {
                kosinski::decode(input, output);
            }
        } else {
            if (moduled) {
                kosinski::moduled_encode(input, output, padding);
            } else {
                kosinski::encode(input, output);
            }
        }
    }

    return 0;
}
