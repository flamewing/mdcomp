// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public
// License along with main.c; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor Boston, MA 02110-1301,  USA

#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstdlib>

#include "getopt.h"
#include "nemesis.h"

static void usage()
{
	std::cerr << "Usage: nemcmp [-i] [-x|--extract [{pointer}]] {input_filename} {output_filename}" << std::endl;
	std::cerr << std::endl;
	std::cerr << "\t-i\tWhen extracting, print out the position where the Nemesis data ends." << std::endl;
	std::cerr << "\t-x,--extract\tExtract from {pointer} address in file." << std::endl << std::endl;
}

int main(int argc, char *argv[])
{
	static struct option long_options[] = {
		{"extract", optional_argument, 0, 'x'},
		{0, 0, 0, 0}
	};

	bool extract = false, printend = false;
	std::streamsize pointer = 0;

	while (true)
	{
		int option_index = 0;
		int c = getopt_long(argc, argv, "x::i",
                            long_options, &option_index);
		if (c == -1)
			break;
		
		switch (c)
		{
			case 'i':
				printend = true;
				break;
			case 'x':
				extract = true;
				if (optarg)
					pointer = strtoul(optarg, 0, 0);
				break;
		}
	}

	if (argc - optind < 2)
	{
		usage();
		return 1;
	}

	std::ifstream fin(argv[optind], std::ios::in|std::ios::binary);
	if (!fin.good())
	{
		std::cerr << "Input file '" << argv[optind] << "' could not be opened." << std::endl << std::endl;
		return 2;
	}

	std::ofstream fout(argv[optind+1], std::ios::out|std::ios::binary);
	if (!fout.good())
	{
		std::cerr << "Output file '" << argv[optind+1] << "' could not be opened." << std::endl << std::endl;
		return 3;
	}

	if (extract)
	{
		int endptr = 0;
		nemesis::decode(fin, fout, pointer, &endptr);
		if (printend)
			std::cout << "0x" << std::hex << std::setw(6) << std::setfill('0') << std::uppercase << std::right << endptr << std::endl;
	}
	else
		nemesis::encode(fin, fout);
	
	return 0;
}
