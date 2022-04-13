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

#include <getopt.h>
#include <mdcomp/enigma.hh>

#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>

using std::cerr;
using std::endl;
using std::fstream;
using std::ifstream;
using std::ios;
using std::ofstream;
using std::stringstream;

static void usage(char* prog) {
    cerr << "Usage: " << prog
         << " [-x|--extract=[{pointer}]] {input_filename} {output_filename}" << endl;
    cerr << endl;
    cerr << "\t-x,--extract\tExtract from {pointer} address in file." << endl << endl;
}

int main(int argc, char* argv[]) {
    constexpr static std::array<option, 2> const long_options{
            option{"extract", optional_argument, nullptr, 'x'},
            option{nullptr, 0, nullptr, 0}};

    bool   extract = false;
    size_t pointer = 0;

    while (true) {
        int option_index = 0;
        int option_char
                = getopt_long(argc, argv, "x::", long_options.data(), &option_index);
        if (option_char == -1) {
            break;
        }

        if (option_char == 'x') {
            extract = true;
            if (optarg != nullptr) {
                pointer = strtoul(optarg, nullptr, 0);
            }
        }
    }

    if (argc - optind < 2) {
        usage(argv[0]);
        return 1;
    }

    ifstream fin(argv[optind], ios::in | ios::binary);
    if (!fin.good()) {
        cerr << "Input file '" << argv[optind] << "' could not be opened." << endl
             << endl;
        return 2;
    }

    ofstream fout(argv[optind + 1], ios::out | ios::binary);
    if (!fout.good()) {
        cerr << "Output file '" << argv[optind + 1] << "' could not be opened." << endl
             << endl;
        return 3;
    }

    if (extract) {
        fin.seekg(static_cast<std::streamsize>(pointer));
        enigma::decode(fin, fout);
    } else {
        enigma::encode(fin, fout);
    }

    return 0;
}
