/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * Copyright (C) Flamewing 2011-2013 <flamewing.sonic@gmail.com>
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

#include "getopt.h"
#include "nemesis.h"

static void usage(char *prog) {
	std::cerr << "Usage: " << prog << " [-i] [-c|--crunch|-x|--extract=[{pointer}]] {input_filename} {output_filename}" << std::endl;
	std::cerr << std::endl;
	std::cerr << "\t-i          \tWhen extracting, print out the position where the Nemesis data ends." << std::endl;
	std::cerr << "\t-x,--extract\tExtract from {pointer} address in file." << std::endl;
	std::cerr << "\t-c,--crunch \tAssume input file is Nemesis-compressed and recompress to output file." << std::endl
	          << "\t            \tIf --chunch is in effect, a missing output_filename means recompress" << std::endl
	          << "\t            \tto input_filename." << std::endl << std::endl;
}

int main(int argc, char *argv[]) {
	static struct option long_options[] = {
		{"extract"   , optional_argument, 0, 'x'},
		{"crunch"    , no_argument      , 0, 'c'},
		{0, 0, 0, 0}
	};

	bool extract = false, printend = false, crunch = false;
	std::streamsize pointer = 0;

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
		std::cerr << "Error: --extract and --crunch can't be used at the same time." << std::endl << std::endl;
		return 4;
	} else if (printend && !extract) {
		std::cerr << "Error: -i must be used with --extract." << std::endl << std::endl;
		return 5;
	}

	char *outfile = crunch && argc - optind < 2 ? argv[optind] : argv[optind + 1];

	std::ifstream fin(argv[optind], std::ios::in | std::ios::binary);
	if (!fin.good()) {
		std::cerr << "Input file '" << argv[optind] << "' could not be opened." << std::endl << std::endl;
		return 2;
	}

	if (crunch) {
		int endptr = 0;
		std::stringstream buffer(std::ios::in | std::ios::out | std::ios::binary);
		nemesis::decode(fin, buffer, pointer, &endptr);
		fin.close();
		buffer.seekg(0);

		std::ofstream fout(outfile, std::ios::out | std::ios::binary);
		if (!fout.good()) {
			std::cerr << "Output file '" << outfile << "' could not be opened." << std::endl << std::endl;
			return 3;
		}
		nemesis::encode(buffer, fout);
	} else {
		std::ofstream fout(outfile, std::ios::out | std::ios::binary);
		if (!fout.good()) {
			std::cerr << "Output file '" << outfile << "' could not be opened." << std::endl << std::endl;
			return 3;
		}

		if (extract) {
			int endptr = 0;
			nemesis::decode(fin, fout, pointer, &endptr);
			if (printend)
				std::cout << "0x" << std::hex << std::setw(6) << std::setfill('0') << std::uppercase << std::right << endptr << std::endl;
		} else
			nemesis::encode(fin, fout);
	}
	return 0;
}
