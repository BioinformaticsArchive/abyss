/** Convert various file formats to FASTQ format.
 * Written by Shaun Jackman <sjackman@bcgsc.ca>.
 * Copyright 2010 Genome Sciences Centre
 */
#include "DataLayer/Options.h"
#include "FastaReader.h"
#include "Uncompress.h"
#include <algorithm>
#include <iostream>

using namespace std;

static void toFASTA(const char* path)
{
	FastaReader in(path, FastaReader::NO_FOLD_CASE
			| FastaReader::CONVERT_QUALITY);
	for (FastaRecord fasta; in >> fasta;)
		cout << fasta;
}

static void toFASTQ(const char* path)
{
	FastaReader in(path, FastaReader::NO_FOLD_CASE
			| FastaReader::CONVERT_QUALITY);
	for (FastqRecord fastq; in >> fastq;)
		cout << fastq;
}

int main(int argc, const char* argv[])
{
	opt::trimMasked = false;
	void (*convert)(const char*)
		= string(argv[0]).find("tofasta") != string::npos
		? toFASTA : toFASTQ;
	if (argc <= 1)
		convert("-");
	else
		for_each(argv + 1, argv + argc, convert);
	return 0;
}
