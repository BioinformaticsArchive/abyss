/** Calculate assembly contiguity statistics.
 * Written by Shaun Jackman <sjackman@bcgsc.ca>.
 */
#include "config.h"
#include "Common/Histogram.h"
#include "Common/IOUtil.h"
#include "DataLayer/FastaReader.h"
#include "DataLayer/Options.h"
#include <algorithm>
#include <getopt.h>
#include <iostream>
#include <sstream>

using namespace std;

#define PROGRAM "abyss-fac"

static const char VERSION_MESSAGE[] =
PROGRAM " (" PACKAGE_NAME ") " VERSION "\n"
"Written by Shaun Jackman.\n"
"\n"
"Copyright 2012 Canada's Michael Smith Genome Science Centre\n";

static const char USAGE_MESSAGE[] =
"Usage: " PROGRAM " [OPTION]... [FILE]...\n"
"Calculate assembly contiguity statistics.\n"
"\n"
"  -s, -t, --min-length=N  ignore sequences shorter than N bp [200]\n"
"  -d, --delimiter=S       use S instead of tab for field delimiter\n"
"  -j, --jira              separate columns with pipes\n"
"      --chastity          discard unchaste sequences [default]\n"
"      --no-chastity       do not discard unchaste sequences\n"
"      --trim-masked       trim masked bases from the end\n"
"      --no-trim-masked    do not trim masked bases from the ends\n"
"                          of sequences [default]\n"
"  -v, --verbose           display verbose output\n"
"      --help              display this help and exit\n"
"      --version           output version information and exit\n"
"\n"
"Report bugs to <" PACKAGE_BUGREPORT ">.\n";

namespace opt {
	static unsigned minLength = 200;
	static string delimiter;
	static bool jira;
	static int verbose;
}

static const char shortopts[] = "d:js:t:v";

enum { OPT_HELP = 1, OPT_VERSION };

static const struct option longopts[] = {
	{ "min-length", no_argument, NULL, 's' },
	{ "delimiter", required_argument, NULL, 'd' },
	{ "jira", no_argument, NULL, 'j' },
	{ "chastity", no_argument, &opt::chastityFilter, 1 },
	{ "no-chastity", no_argument, &opt::chastityFilter, 0 },
	{ "trim-masked", no_argument, &opt::trimMasked, 1 },
	{ "no-trim-masked", no_argument, &opt::trimMasked, 0 },
	{ "help", no_argument, NULL, OPT_HELP },
	{ "version", no_argument, NULL, OPT_VERSION },
	{ NULL, 0, NULL, 0 }
};

/** FastaReader flags. */
static const int FASTAREADER_FLAGS = FastaReader::FOLD_CASE;

/** Print contiguity statistics. */
static void printContiguityStatistics(const char* path)
{
	// Read the sequences and count the lengths.
	Histogram h;
	FastaReader in(path, FASTAREADER_FLAGS);
	for (FastaRecord record; in >> record;)
		h.insert(record.seq.size());
	assert(in.eof());

	// Print the table header.
	static bool printHeader = true;
	if (opt::jira && printHeader) {
		printHeader = false;
		const char* sep = "\t||";
		cout << "||"
			<< "n" << sep
			<< "n:" << opt::minLength << sep
			<< "n:N50" << sep
			<< "min" << sep
			<< "N80" << sep
			<< "N50" << sep
			<< "N20" << sep
			<< "max" << sep
			<< "sum" << sep << '\n';
	}

	// Print the table.
	if (opt::jira)
		cout << '|';
	printContiguityStats(cout, h, opt::minLength,
			printHeader, opt::delimiter)
		<< opt::delimiter << path;
	if (opt::jira)
		cout << opt::delimiter;
	cout << '\n';
	printHeader = false;
}

int main(int argc, char** argv)
{
	opt::trimMasked = false;

	bool die = false;
	for (int c; (c = getopt_long(argc, argv,
					shortopts, longopts, NULL)) != -1;) {
		istringstream arg;
		if (optarg != NULL)
			arg.str(optarg);
		switch (c) {
		  case '?':
			die = true;
			break;
		  case 'd':
			arg >> opt::delimiter;
			break;
		  case 'j':
			opt::delimiter = "\t|";
			opt::jira = true;
			break;
		  case 's': case 't':
			arg >> opt::minLength;
			break;
		  case 'v':
			opt::verbose++;
			break;
		  case OPT_HELP:
			cout << USAGE_MESSAGE;
			exit(EXIT_SUCCESS);
		  case OPT_VERSION:
			cout << VERSION_MESSAGE;
			exit(EXIT_SUCCESS);
		}
		if (optarg != NULL && !arg.eof()) {
			cerr << PROGRAM ": invalid option: `-"
				<< (char)c << optarg << "'\n";
			exit(EXIT_FAILURE);
		}
	}
	if (die) {
		cerr << "Try `" << PROGRAM
			<< " --help' for more information.\n";
		exit(EXIT_FAILURE);
	}

	if (optind == argc)
		printContiguityStatistics("-");
	else
		for_each(argv + optind, argv + argc,
				printContiguityStatistics);

	cout.flush();
	assert_good(cout, "stdout");
	return 0;
}
