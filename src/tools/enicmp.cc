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
#include <fstream>
#include <cstdlib>

#include "getopt.h"
#include "enigma.h"

static void usage(char *prog) {
	std::cerr << "Usage: " << prog << " [-x|--extract=[{pointer}]] [-p|--padding] {input_filename} {output_filename}" << std::endl;
	std::cerr << std::endl;
	std::cerr << "\t-x,--extract\tExtract from {pointer} address in file." << std::endl;
	std::cerr << "\t-p,--padding\tAdd or remove padding. Use this only for Sonic 1 Special Stage files in 80x80 block mode" << std::endl << std::endl;
}

int main(int argc, char *argv[]) {
	static struct option long_options[] = {
		{"extract", optional_argument, 0, 'x'},
		{"padding", no_argument      , 0, 'p'},
		{0, 0, 0, 0}
	};

	bool extract = false, padding = false;
	std::streamsize pointer = 0;

	while (true) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "x::p",
		                    long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
			case 'x':
				extract = true;
				if (optarg)
					pointer = strtoul(optarg, 0, 0);
				break;

			case 'p':
				padding = true;
				break;
		}
	}

	if (argc - optind < 2) {
		usage(argv[0]);
		return 1;
	}

	std::ifstream fin(argv[optind], std::ios::in | std::ios::binary);
	if (!fin.good()) {
		std::cerr << "Input file '" << argv[optind] << "' could not be opened." << std::endl << std::endl;
		return 2;
	}

	std::ofstream fout(argv[optind + 1], std::ios::out | std::ios::binary);
	if (!fout.good()) {
		std::cerr << "Output file '" << argv[optind + 1] << "' could not be opened." << std::endl << std::endl;
		return 3;
	}

	if (extract)
		enigma::decode(fin, fout, pointer, padding);
	else
		enigma::encode(fin, fout, padding);

	return 0;
}
