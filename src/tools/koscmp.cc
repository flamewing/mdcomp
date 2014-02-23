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
#include <sstream>
#include <cstdlib>

#include "getopt.h"
#include "kosinski.h"

static void usage(char *prog) {
	std::cerr << "Usage: " << prog << " [-c|--crunch|-x|--extract=[{pointer}]] [-r {reclen}] [-s {slidewin}] [-m|--moduled=[{size}]] [-p|--padding=[{len}]] {input_filename} {output_filename}" << std::endl;
	std::cerr << std::endl;
	std::cerr << "\t-x,--extract\tExtract from {pointer} address in file." << std::endl;
	std::cerr << "\t-c,--crunch \tAssume input file is Kosinski-compressed and recompress to output file." << std::endl
	          << "\t            \tIf --chunch is in effect, a missing output_filename means recompress" << std::endl
	          << "\t            \tto input_filename. All parameters affect only the output file, except" << std::endl
	          << "\t            \tfor the -m parameter, which makes both input and output files moduled" << std::endl
	          << "\t            \t(but the optional module size affects only the output file)." << std::endl;
	std::cerr << "\t-m,--moduled\tUse compression in modules (S3&K). {size} only affects compression; it is" << std::endl
	          << "\t            \tthe size of each module (defaults to 0x1000 if ommitted)." << std::endl;
	std::cerr << "\t-p|--padding\tFor moduled compression only. Changes internal module padding to {len}." << std::endl
	          << "\t            \tEach module will be padded to a multiple of the given number; use 1 for" << std::endl
	          << "\t            \tno padding. Must be a power of 2 (defaults to 16 if ommitted)." << std::endl;
	std::cerr << "\t-r          \tSet recursion length (default/maximum: 256)" << std::endl;
	std::cerr << "\t-s          \tSets sliding window size (default/maximum: 8192)" << std::endl << std::endl;
}

int main(int argc, char *argv[]) {
	static struct option long_options[] = {
		{"extract", optional_argument, 0, 'x'},
		{"moduled", optional_argument, 0, 'm'},
		{"crunch" , no_argument      , 0, 'c'},
		{"padding", required_argument, 0, 'p'},
		{0, 0, 0, 0}
	};

	bool extract = false, moduled = false, crunch = false;
	std::streamsize pointer = 0, slidewin = 8192, reclen = 256,
	                modulesize = 0x1000, padding = 16;

	while (true) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "x::m::cr:s:p:",
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
			case 'm':
				moduled = true;
				if (optarg)
					modulesize = strtoul(optarg, 0, 0);
				if (!modulesize)
					modulesize = 0x1000;
				break;
			case 'r':
				if (optarg)
					reclen = strtoul(optarg, 0, 0);
				if (!reclen || reclen > 256)
					reclen = 256;
				break;
			case 's':
				if (optarg)
					slidewin = strtoul(optarg, 0, 0);
				if (!slidewin || slidewin > 8192)
					slidewin = 8192;
				break;
			case 'p':
				if (optarg)
					padding = strtoul(optarg, 0, 0);
				if (!padding || (padding & (padding - 1)) != 0)
					padding = 16;
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
	}

	char *outfile = crunch && argc - optind < 2 ? argv[optind] : argv[optind + 1];

	std::ifstream fin(argv[optind], std::ios::in | std::ios::binary);
	if (!fin.good()) {
		std::cerr << "Input file '" << argv[optind] << "' could not be opened." << std::endl << std::endl;
		return 2;
	}

	if (crunch) {
		std::stringstream buffer(std::ios::in | std::ios::out | std::ios::binary);
		kosinski::decode(fin, buffer, pointer, moduled, padding);
		fin.close();
		buffer.seekg(0);

		std::fstream fout(outfile, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
		if (!fout.good()) {
			std::cerr << "Output file '" << argv[optind + 1] << "' could not be opened." << std::endl << std::endl;
			return 3;
		}
		kosinski::encode(buffer, fout, slidewin, reclen, moduled, modulesize, padding);
	} else {
		std::fstream fout(outfile, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
		if (!fout.good()) {
			std::cerr << "Output file '" << argv[optind + 1] << "' could not be opened." << std::endl << std::endl;
			return 3;
		}

		if (extract)
			kosinski::decode(fin, fout, pointer, moduled, padding);
		else
			kosinski::encode(fin, fout, slidewin, reclen, moduled, modulesize, padding);
	}

	return 0;
}
