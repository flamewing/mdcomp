/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * Copyright (C) Flamewing 2013 <flamewing.sonic@gmail.com>
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

#include "getopt.h"
#include "comper.h"

using namespace std;

static void usage(char *prog) {
	cerr << "Usage: " << prog << " [-c|--crunch|-x|--extract=[{pointer}]] {input_filename} {output_filename}" << endl;
	cerr << endl;
	cerr << "\t-x,--extract\tExtract from {pointer} address in file." << endl;
	cerr << "\t-c,--crunch \tAssume input file is Comper-compressed and recompress to output file." << endl
	     << "\t            \tIf --chunch is in effect, a missing output_filename means recompress" << endl
	     << "\t            \tto input_filename." << endl << endl;
}

int main(int argc, char *argv[]) {
	static option long_options[] = {
		{"extract", optional_argument, 0, 'x'},
		{"crunch" , no_argument      , 0, 'c'},
		{0, 0, 0, 0}
	};

	bool extract = false, crunch = false;
	streamsize pointer = 0;

	while (true) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "x::c",
		                    long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
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
	}

	char *outfile = crunch && argc - optind < 2 ? argv[optind] : argv[optind + 1];

	ifstream fin(argv[optind], ios::in | ios::binary);
	if (!fin.good()) {
		cerr << "Input file '" << argv[optind] << "' could not be opened." << endl << endl;
		return 2;
	}

	if (crunch) {
		stringstream buffer(ios::in | ios::out | ios::binary);
		comper::decode(fin, buffer, pointer);
		fin.close();
		buffer.seekg(0);

		fstream fout(outfile, ios::in | ios::out | ios::binary | ios::trunc);
		if (!fout.good()) {
			cerr << "Output file '" << argv[optind + 1] << "' could not be opened." << endl << endl;
			return 3;
		}
		comper::encode(buffer, fout);
	} else {
		fstream fout(outfile, ios::in | ios::out | ios::binary | ios::trunc);
		if (!fout.good()) {
			cerr << "Output file '" << argv[optind + 1] << "' could not be opened." << endl << endl;
			return 3;
		}

		if (extract)
			comper::decode(fin, fout, pointer);
		else
			comper::encode(fin, fout);
	}

	return 0;
}
