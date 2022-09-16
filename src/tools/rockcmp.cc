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

#include "mdcomp/rocket.hh"

#include <getopt.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

using std::cerr;
using std::endl;
using std::fstream;
using std::ifstream;
using std::ios;
using std::stringstream;

static void usage(char* prog) {
    cerr << "Usage: " << prog
         << " [-c|--crunch|-x|--extract=[{pointer}]] {input_filename} "
            "{output_filename}"
         << endl;
    cerr << endl;
    cerr << "\t-x,--extract\tExtract from {pointer} address in file." << endl;
    cerr << "\t-c,--crunch \tAssume input file is Rocket-compressed and "
            "recompress to output file."
         << endl
         << "\t            \tIf --crunch is in effect, a missing "
            "output_filename means recompress"
         << endl
         << "\t            \tto input_filename." << endl
         << endl;
}

int main(int argc, char* argv[]) {
    constexpr static std::array const long_options{
            option{"extract", optional_argument, nullptr, 'x'},
            option{ "crunch",       no_argument, nullptr, 'c'},
            option{  nullptr,                 0, nullptr,   0}
    };

    bool   extract = false;
    bool   crunch  = false;
    size_t pointer = 0;

    while (true) {
        int       option_index = 0;
        int const option_char
                = getopt_long(argc, argv, "x::c", long_options.data(), &option_index);
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
        default:
            break;
        }
    }

    if ((!crunch && argc - optind < 2) || (crunch && argc - optind < 1)) {
        usage(argv[0]);
        return 1;
    }

    if (extract && crunch) {
        cerr << "Error: --extract and --crunch can't be used at the same time." << endl
             << endl;
        return 4;
    }

    char const* outfile = crunch && argc - optind < 2 ? argv[optind] : argv[optind + 1];

    ifstream input(argv[optind], ios::in | ios::binary);
    if (!input.good()) {
        cerr << "Input file '" << argv[optind] << "' could not be opened." << endl
             << endl;
        return 2;
    }

    if (crunch) {
        stringstream buffer(ios::in | ios::out | ios::binary);
        input.seekg(static_cast<std::streamsize>(pointer));
        rocket::decode(input, buffer);
        input.close();
        buffer.seekg(0);

        fstream output(outfile, ios::in | ios::out | ios::binary | ios::trunc);
        if (!output.good()) {
            cerr << "Output file '" << argv[optind + 1] << "' could not be opened."
                 << endl
                 << endl;
            return 3;
        }
        rocket::encode(buffer, output);
    } else {
        fstream output(outfile, ios::in | ios::out | ios::binary | ios::trunc);
        if (!output.good()) {
            cerr << "Output file '" << argv[optind + 1] << "' could not be opened."
                 << endl
                 << endl;
            return 3;
        }

        if (extract) {
            input.seekg(static_cast<std::streamsize>(pointer));
            rocket::decode(input, output);
        } else {
            rocket::encode(input, output);
        }
    }

    return 0;
}
