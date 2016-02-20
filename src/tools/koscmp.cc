/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
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

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>

#include <getopt.h>

#include "kosinski.h"

using namespace std;

static void usage(char *prog) {
	cerr << "Usage: " << prog << " [-c|--crunch|-x|--extract=[{pointer}]] [-m|--moduled=[{size}]] [-p|--padding=[{len}]] {input_filename} {output_filename}" << endl;
	cerr << endl;
	cerr << "\t-x,--extract\tExtract from {pointer} address in file." << endl;
	cerr << "\t-c,--crunch \tAssume input file is Kosinski-compressed and recompress to output file." << endl
	     << "\t            \tIf --chunch is in effect, a missing output_filename means recompress" << endl
	     << "\t            \tto input_filename. All parameters affect only the output file, except" << endl
	     << "\t            \tfor the -m parameter, which makes both input and output files moduled" << endl
	     << "\t            \t(but the optional module size affects only the output file)." << endl;
	cerr << "\t-m,--moduled\tUse compression in modules (S3&K). {size} only affects compression; it is" << endl
	     << "\t            \tthe size of each module (defaults to 0x1000 if ommitted)." << endl;
	cerr << "\t-p|--padding\tFor moduled compression only. Changes internal module padding to {len}." << endl
	     << "\t            \tEach module will be padded to a multiple of the given number; use 1 for" << endl
	     << "\t            \tno padding. Must be a power of 2 (defaults to 16 if ommitted)." << endl << endl;
}

int main(int argc, char *argv[]) {
	static option long_options[] = {
		{"extract", optional_argument, nullptr, 'x'},
		{"moduled", optional_argument, nullptr, 'm'},
		{"crunch" , no_argument      , nullptr, 'c'},
		{"padding", required_argument, nullptr, 'p'},
		{nullptr, 0, nullptr, 0}
	};

	bool extract = false, moduled = false, crunch = false;
	streamsize pointer = 0, modulesize = 0x1000, padding = 16;

	while (true) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "x::m::cr:s:p:",
		                    long_options, &option_index);
		if (c == -1) {
			break;
		}

		switch (c) {
			case 'x':
				extract = true;
				if (optarg) {
					pointer = strtoul(optarg, nullptr, 0);
				}
				break;
			case 'c':
				crunch = true;
				break;
			case 'm':
				moduled = true;
				if (optarg) {
					modulesize = strtoul(optarg, nullptr, 0);
				}
				if (!modulesize) {
					modulesize = 0x1000;
				}
				break;
			case 'p':
				if (optarg) {
					padding = strtoul(optarg, nullptr, 0);
				}
				if (!padding || (padding & (padding - 1)) != 0) {
					padding = 16;
				}
				break;
		}
	}

	if ((!crunch && argc - optind < 2) || (crunch && argc - optind < 1)) {
		usage(argv[0]);
		return 1;
	}

	if (extract && crunch) {
		cerr << "Error: --extract and --crunch can't be used at the same time." << endl << endl;
		return 4;
	}

	char *outfile = crunch && argc - optind < 2 ? argv[optind] : argv[optind + 1];

	ifstream fin(argv[optind], ios::in | ios::binary);
	if (!fin.good()) {
		cerr << "Input file '" << argv[optind] << "' could not be opened." << endl << endl;
		return 2;
	}

	if (crunch) {
		stringstream buffer(ios::in | ios::out | ios::binary);
		kosinski::decode(fin, buffer, pointer, moduled, padding);
		fin.close();
		buffer.seekg(0);

		fstream fout(outfile, ios::in | ios::out | ios::binary | ios::trunc);
		if (!fout.good()) {
			cerr << "Output file '" << argv[optind + 1] << "' could not be opened." << endl << endl;
			return 3;
		}
		kosinski::encode(buffer, fout, moduled, modulesize, padding);
	} else {
		fstream fout(outfile, ios::in | ios::out | ios::binary | ios::trunc);
		if (!fout.good()) {
			cerr << "Output file '" << argv[optind + 1] << "' could not be opened." << endl << endl;
			return 3;
		}

		if (extract) {
			kosinski::decode(fin, fout, pointer, moduled, padding);
		} else {
			kosinski::encode(fin, fout, moduled, modulesize, padding);
		}
	}

	return 0;
}
