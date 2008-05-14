/** Written by Shaun Jackman <sjackman@bcgsc.ca>. */

#include "config.h"
#include <getopt.h>
#include <iostream>
#include <sstream>

using namespace std;

namespace opt {

static const char *VERSION_MESSAGE =
PACKAGE " (ABySS) " VERSION "\n"
"Written by Jared Simpson.\n"
"\n"
"Copyright 2008 Canada's Michael Smith Genome Science Centre\n";

static const char *USAGE_MESSAGE =
"Usage: " PACKAGE " [OPTION]... [ FASTA FILE | PACKED SEQ FILE(.psq)] ...\n"
"Assemble FASTA_FILE.\n"
"\n"
"  -k, --kmer=KMER_SIZE           k-mer size\n"
"  -l, --read-length=READ_LENGTH  read length\n"
"  -t, --trim-length=TRIM_LENGTH  maximum length of dangling\n"
"                                 edges to trim\n"
"  -d, --disable-erosion          disable the erosion step, this will give\n"
"                                 slightly shorter contigs, but less errors at the ends of contigs\n"
"  -g, --graph                    generate a graph in dot format\n"
"  -v, --verbose                  display verbose output\n"
"      --help     display this help and exit\n"
"      --version  output version information and exit\n"
"\n"
"Report bugs to " PACKAGE_BUGREPORT "\n";

/** k-mer length */
int kmerSize = -1;

/** read length */
int readLen = -1;

/** trim length */
int trimLen = -1;

/** erosion flag */
bool disableErosion = false;

/** graph output */
std::string graphPath;

/** verbose output */
int verbose = 0;

/** input FASTA files */
std::string inFile;

static const char *shortopts = "k:l:t:dg:v";

enum { OPT_HELP = 1, OPT_VERSION };

static const struct option longopts[] = {
	{ "kmer",        required_argument, NULL, 'k' },
	{ "read-length", required_argument, NULL, 'l' },
	{ "trim-length", required_argument, NULL, 't' },
	{ "disable-erosion", no_argument,   NULL, 'd' },
	{ "graph",       required_argument, NULL, 'g' },
	{ "verbose",     no_argument,       NULL, 'v' },
	{ "help",        no_argument,       NULL, OPT_HELP },
	{ "version",     no_argument,       NULL, OPT_VERSION },
	{ NULL, 0, NULL, 0 }
};

/** Parse the specified command line. */
void parse(int argc, char* const* argv)
{
	char c;
	while ((c = getopt_long(argc, argv, shortopts, longopts, NULL))
			!= -1) {
		istringstream arg;
		if (optarg != NULL)
			arg.str(optarg);
		switch (c) {
			case 'k':
				arg >> kmerSize;
				break;
			case 'l':
				arg >> readLen;
				break;
			case 't':
				arg >> trimLen;
				break;
			case 'd':
				disableErosion = true;
				break;	
			case 'g':
				graphPath = optarg;
				break;
			case 'v':
				verbose++;
				break;
			case OPT_HELP:
				cout << USAGE_MESSAGE;
				exit(EXIT_SUCCESS);
			case OPT_VERSION:
				cout << VERSION_MESSAGE;
				exit(EXIT_SUCCESS);
		}
	}

	bool die = false;
	if (kmerSize <= 0) {
		cerr << PACKAGE ": " << "missing -k,--kmer option\n";
		die = true;
	}
	if (readLen <= 0) {
		cerr << PACKAGE ": " << "missing -l,--read-length option\n";
		die = true;
	}
	if (argc - optind > 1) {
		cerr << PACKAGE ": " << "unexpected argument: "
			<< argv[optind+1] << "\n";
		die = true;
	}
	if (argv[optind] == NULL) {
		cerr << PACKAGE ": " << "missing input sequence file argument\n";
		die = true;
	}
	if (die) {
		cerr << "Try `" << PACKAGE
			<< " --help' for more information.\n";
		exit(EXIT_FAILURE);
	}
	if (kmerSize > readLen) {
		cerr << PACKAGE ": "
			<< "k-mer size must be smaller than the read length\n";
		exit(EXIT_FAILURE);
	}

	inFile = argv[optind++];

	if (trimLen < 0)
		trimLen = 6 * (readLen - kmerSize + 1);
		

}

} // namespace opt
