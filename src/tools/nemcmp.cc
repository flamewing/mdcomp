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
#include <iomanip>
#include <fstream>
#include <sstream>
#include <cstdlib>

#include <getopt.h>

#include "nemesis.h"

using namespace std;

static void usage(char *prog) {
	cerr << "Usage: " << prog << " [-i] [-c|--crunch|-x|--extract=[{pointer}]] {input_filename} {output_filename}" << endl;
	cerr << endl;
	cerr << "\t-i          \tWhen extracting, print out the position where the Nemesis data ends." << endl;
	cerr << "\t-x,--extract\tExtract from {pointer} address in file." << endl;
	cerr << "\t-c,--crunch \tAssume input file is Nemesis-compressed and recompress to output file." << endl
	     << "\t            \tIf --chunch is in effect, a missing output_filename means recompress" << endl
	     << "\t            \tto input_filename." << endl << endl;
}

int main(int argc, char *argv[]) {
	static option long_options[] = {
		{"extract"   , optional_argument, 0, 'x'},
		{"crunch"    , no_argument      , 0, 'c'},
		{0, 0, 0, 0}
	};

	bool extract = false, printend = false, crunch = false;
	streamsize pointer = 0;

	while (true) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "x::ic",
		                    long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
			case 'i':
				printend = true;
				break;
			case 'x':
				extract = true;
				if (optarg)
					pointer = strtoul(optarg, 0, 0);
				break;
			case 'c':
				crunch = true;
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
	} else if (printend && !extract) {
		cerr << "Error: -i must be used with --extract." << endl << endl;
		return 5;
	}

	char *outfile = crunch && argc - optind < 2 ? argv[optind] : argv[optind + 1];

	ifstream fin(argv[optind], ios::in | ios::binary);
	if (!fin.good()) {
		cerr << "Input file '" << argv[optind] << "' could not be opened." << endl << endl;
		return 2;
	}

	if (crunch) {
		int endptr = 0;
		stringstream buffer(ios::in | ios::out | ios::binary);
		nemesis::decode(fin, buffer, pointer, &endptr);
		fin.close();
		buffer.seekg(0);

		ofstream fout(outfile, ios::out | ios::binary);
		if (!fout.good()) {
			cerr << "Output file '" << outfile << "' could not be opened." << endl << endl;
			return 3;
		}
		nemesis::encode(buffer, fout);
	} else {
		ofstream fout(outfile, ios::out | ios::binary);
		if (!fout.good()) {
			cerr << "Output file '" << outfile << "' could not be opened." << endl << endl;
			return 3;
		}

		if (extract) {
			int endptr = 0;
			nemesis::decode(fin, fout, pointer, &endptr);
			if (printend)
				cout << "0x" << hex << setw(6) << setfill('0') << uppercase << right << endptr << endl;
		} else
			nemesis::encode(fin, fout);
	}
	return 0;
}
