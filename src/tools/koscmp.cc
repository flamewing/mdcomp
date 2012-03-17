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
#include <fstream>
#include <cstdlib>

#include "getopt.h"
#include "kosinski.h"

static void usage()
{
	std::cerr << "Usage: koscmp [-x|--extract [{pointer}]] [-r {reclen}] [-s {slidewin}] [-m|--moduled [{size}]] {input_filename} {output_filename}" << std::endl;
	std::cerr << std::endl;
	std::cerr << "\t-x,--extract\tExtract from {pointer} address in file." << std::endl;
	std::cerr << "\t-m,--moduled\tUse compression in modules (S3&K). {size} only affects compression; it is" << std::endl
	          << "\t            \tthe size of each module (defaults to 0x1000 if ommitted)." << std::endl;
	std::cerr << "\t-r\t\tSet recursion length (default: 256)" << std::endl;
	std::cerr << "\t-s\t\tSets sliding window size (default: 8192)" << std::endl << std::endl;
}

int main(int argc, char *argv[])
{
	static struct option long_options[] = {
		{"extract", optional_argument, 0, 'x'},
		{"moduled", optional_argument, 0, 'm'},
		{0, 0, 0, 0}
	};

	bool extract = false, moduled = false;
	std::streamsize pointer = 0, slidewin = 8192, reclen = 256, modulesize = 0x1000;

	while (true)
	{
		int option_index = 0;
		int c = getopt_long(argc, argv, "x::mr:s:",
                            long_options, &option_index);
		if (c == -1)
			break;
		
		switch (c)
		{
			case 'x':
				extract = true;
				if (optarg)
					pointer = strtoul(optarg, 0, 0);
				break;
				
			case 'm':
				moduled = true;
				if (optarg)
					modulesize = strtoul(optarg, 0, 0);
				break;

			case 'r':
				if (optarg)
					reclen = strtoul(optarg, 0, 0);

				if (!reclen)
					reclen = 256;
				break;

			case 's':
				if (optarg)
					slidewin = strtoul(optarg, 0, 0);

				if (!slidewin)
					slidewin = 8192;
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

	std::fstream fout(argv[optind+1], std::ios::in|std::ios::out|std::ios::binary|std::ios::trunc);
	if (!fout.good())
	{
		std::cerr << "Output file '" << argv[optind+1] << "' could not be opened." << std::endl << std::endl;
		return 3;
	}

	if (extract)
		kosinski::decode(fin, fout, pointer, moduled);
	else
		kosinski::encode(fin, fout, slidewin, reclen, moduled, modulesize);
	
	return 0;
}
