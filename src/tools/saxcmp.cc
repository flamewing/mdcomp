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
#include "saxman.h"

static void usage() {
	std::cerr << "Usage: saxcmp [-s size|-S] [-c|--crunch|-x|--extract=[{pointer}]] {input_filename} {output_filename}" << std::endl;
	std::cerr << std::endl;
	std::cerr << "\t-x,--extract\tExtract from {pointer} address in file." << std::endl;
	std::cerr << "\t-c,--crunch \tAssume input file is Saxman-compressed and recompress to output file." << std::endl
	          << "\t            \tIf --chunch is in effect, a missing output_filename means recompress" << std::endl
	          << "\t            \tto input_filename." << std::endl
	          << "\t-s size     \tAssume input file does not have a file size anduse value given instead." << std::endl
	          << "\t            \tOnly affects decompression." << std::endl
	          << "\t-S          \tCauses the compressor to not output a file size. Only affects compression." << std::endl << std::endl;
}

int main(int argc, char *argv[]) {
	static struct option long_options[] = {
		{"extract", optional_argument, 0, 'x'},
		{"crunch" , no_argument      , 0, 'c'},
		{0, 0, 0, 0}
	};

	bool extract = false, crunch = false, WithSize = true;
	std::streamsize pointer = 0, BSize = 0;
	
	while (true) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "x::cs:S",
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
			case 's':
				BSize = strtoul(optarg, 0, 0);
				if (BSize == 0) {
					std::cerr << "Error: specified size must be a positive number." << std::endl << std::endl;
					return 4;
				}
				break;
			case 'S':
				WithSize = false;
				break;
		}
	}

	if (argc - optind < 2 || (crunch && argc - optind < 1)) {
		usage();
		return 1;
	}

	if (extract && crunch) {
		std::cerr << "Error: --extract and --crunch can't be used at the same time." << std::endl << std::endl;
		return 4;
	}

	char *outfile = crunch && argc - optind < 2 ? argv[optind] : argv[optind + 1];

	std::ifstream fin(argv[optind], std::ios::in | std::ios::binary);
	if (!fin.good()) {
		std::cerr << "Input file '" << argv[optind] << "' could not be opened." << std::endl << std::endl;
		return 2;
	}

	if (crunch) {
		std::stringstream buffer(std::ios::in | std::ios::out | std::ios::binary);
		saxman::decode(fin, buffer, pointer);
		fin.close();
		buffer.seekg(0);

		std::fstream fout(outfile, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
		if (!fout.good()) {
			std::cerr << "Output file '" << argv[optind + 1] << "' could not be opened." << std::endl << std::endl;
			return 3;
		}
		saxman::encode(buffer, fout);
	} else {
		std::fstream fout(outfile, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
		if (!fout.good()) {
			std::cerr << "Output file '" << argv[optind + 1] << "' could not be opened." << std::endl << std::endl;
			return 3;
		}

		if (extract)
			saxman::decode(fin, fout, pointer, BSize);
		else
			saxman::encode(fin, fout, WithSize);
	}

	return 0;
}
