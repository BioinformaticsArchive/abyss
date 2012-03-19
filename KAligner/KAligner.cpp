#include "Aligner.h"
#include "PairedAlgorithms.h"
#include "PrefixIterator.h"
#include "FastaReader.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <getopt.h>
#include <sstream>
#include <string>
#include <pthread.h>
#include <semaphore.h>

using namespace std;

#define PROGRAM "KAligner"

static const char *VERSION_MESSAGE =
PROGRAM " (ABySS) " VERSION "\n"
"Written by Jared Simpson and Shaun Jackman.\n"
"\n"
"Copyright 2009 Canada's Michael Smith Genome Science Centre\n";

static const char *USAGE_MESSAGE =
"Usage: " PROGRAM " [OPTION]... QUERY TARGET\n"
"Align the sequences of QUERY against those of TARGET.\n"
"All perfect matches of at least k bases will be found.\n"
"\n"
"  -k, --kmer=KMER_SIZE  k-mer size\n"
"  -m, --multimap        allow duplicate k-mer in the target\n"
"      --no-multimap     disallow duplicate k-mer in the target [default]\n"
"  -j, --threads=THREADS the max number of threads created\n"
"                        set to 0 for one thread per reads file\n"
"  -v, --verbose         display verbose output\n"
"      --seq             print the sequence with the alignments\n"
"      --help            display this help and exit\n"
"      --version         output version information and exit\n"
"\n"
"Report bugs to <" PACKAGE_BUGREPORT ">.\n";

namespace opt {
	static unsigned k;
	int multimap; // used by Aligner
	static int threads = 1;
	static int verbose;
	extern bool colourSpace;
	static bool printSeq = false;
	extern int chastityFilter; // used by FastaReader
}

static const char* shortopts = "k:mo:j:v";

enum { OPT_HELP = 1, OPT_VERSION, OPT_SEQ };

static const struct option longopts[] = {
	{ "kmer",        required_argument, NULL, 'k' },
	{ "multimap",    no_argument,       &opt::multimap, 1 },
	{ "no-multi",    no_argument,       &opt::multimap, 0 },
	{ "threads",     required_argument,	NULL, 'j' },
	{ "verbose",     no_argument,       NULL, 'v' },
	{ "seq",		 no_argument,		NULL, OPT_SEQ },
	{ "help",        no_argument,       NULL, OPT_HELP },
	{ "version",     no_argument,       NULL, OPT_VERSION },
	{ NULL, 0, NULL, 0 }
};

template <class SeqPosHashMap>
static void readContigsIntoDB(string refFastaFile,
		Aligner<SeqPosHashMap>& aligner);
void *alignReadsToDB(void *arg);

/** Unique aligner using map */
static Aligner<SeqPosHashUniqueMap> *g_aligner_u;

/** Multimap aligner using multimap */
static Aligner<SeqPosHashMultiMap> *g_aligner_m;

static unsigned g_readCount;
static pthread_mutex_t g_mutexCout, g_mutexCerr;
static sem_t g_activeThreads;

static pthread_t getReadFiles(const char *readsFile)
{
	// Ensure we don't create more than opt::threads threads at a time.
	if (opt::threads > 0)
		sem_wait(&g_activeThreads);

	if (opt::verbose > 0) {
		pthread_mutex_lock(&g_mutexCerr);
		cerr << "Reading `" << readsFile << "'...\n";
		pthread_mutex_unlock(&g_mutexCerr);
	}

	pthread_t thread;
	pthread_create(&thread, NULL, alignReadsToDB, (void*)readsFile);
	return thread;
}

int main(int argc, char** argv)
{
	bool die = false;
	for (int c; (c = getopt_long(argc, argv,
					shortopts, longopts, NULL)) != -1;) {
		istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
			case '?': die = true; break;
			case 'k': arg >> opt::k; break;
			case 'm': opt::multimap = 1; break;
			case 'j': arg >> opt::threads; break;
			case 'v': opt::verbose++; break;
			case OPT_SEQ: opt::printSeq = true; break;
			case OPT_HELP:
				cout << USAGE_MESSAGE;
				exit(EXIT_SUCCESS);
			case OPT_VERSION:
				cout << VERSION_MESSAGE;
				exit(EXIT_SUCCESS);
		}
	}

	if (opt::k <= 0) {
		cerr << PROGRAM ": missing -k,--kmer option\n";
		die = true;
	}

	if (argc - optind < 2) {
		cerr << PROGRAM ": missing arguments\n";
		die = true;
	}

	if (die) {
		cerr << "Try `" << PROGRAM
			<< " --help' for more information.\n";
		exit(EXIT_FAILURE);
	}

	string refFastaFile(argv[argc - 1]);

	if (opt::verbose > 0)
		cerr << "k: " << opt::k
			<< " Target: " << refFastaFile
			<< endl;

	if (opt::multimap) {
		g_aligner_m = new Aligner<SeqPosHashMultiMap>(opt::k, 1<<26);
		readContigsIntoDB(refFastaFile, *g_aligner_m);
	} else {
#if HAVE_GOOGLE_SPARSE_HASH_SET
		g_aligner_u = new Aligner<SeqPosHashUniqueMap>(opt::k, 1<<28);
		g_aligner_u->max_load_factor(0.2);
#else
		g_aligner_u = new Aligner<SeqPosHashUniqueMap>(opt::k, 1<<26);
#endif
		readContigsIntoDB(refFastaFile, *g_aligner_u);
	}

	// Need to initialize mutex's before threads are created.
	pthread_mutex_init(&g_mutexCout, NULL);
	pthread_mutex_init(&g_mutexCerr, NULL);
	if (opt::threads > 0)
		sem_init(&g_activeThreads, 0, opt::threads);

	g_readCount = 0;
	vector<pthread_t> threads;
	transform(argv + optind, argv + argc - 1, back_inserter(threads),
			getReadFiles);

	void *status;
	// Wait for all threads to finish.
	for (size_t i = 0; i < threads.size(); i++)
		pthread_join(threads[i], &status);

	if (opt::verbose > 0)
		cerr << "Aligned " << g_readCount << " reads\n";

	if (opt::multimap)
		delete g_aligner_m;
	else
		delete g_aligner_u;

	return 0;
}

static void assert_open(ifstream& f, const string& p)
{
	if (f.is_open())
		return;
	cerr << p << ": " << strerror(errno) << endl;
	exit(EXIT_FAILURE);
}

template <class SeqPosHashMap>
static void printProgress(const Aligner<SeqPosHashMap>& align,
		unsigned count)
{
	size_t size = align.size();
	size_t buckets = align.bucket_count();
	cerr << "Read " << count << " contigs. "
		<< "Hash load: " << size <<
		" / " << buckets << " = " << (float)size / buckets << endl;
}

template <class SeqPosHashMap>
static void readContigsIntoDB(string refFastaFile,
		Aligner<SeqPosHashMap>& aligner)
{
	int count = 0;
	ifstream fileHandle(refFastaFile.c_str());
	assert_open(fileHandle, refFastaFile);

	while(!fileHandle.eof() && fileHandle.peek() != EOF)
	{
		ContigID contigID;
		Sequence seq;
		int length;
		double coverage;

		PairedAlgorithms::parseContigFromFile(fileHandle, contigID, seq, length, coverage);

		if (count == 0) {
			// Detect colour-space contigs.
			opt::colourSpace = isdigit(seq[0]);
		} else {
			if (opt::colourSpace)
				assert(isdigit(seq[0]));
			else
				assert(isalpha(seq[0]));
		}

		aligner.addReferenceSequence(contigID, seq);

		count++;
		if (opt::verbose > 0 && count % 100000 == 0)
			printProgress(aligner, count);
	}
	if (opt::verbose > 0)
			printProgress(aligner, count);

	fileHandle.close();
}

void *alignReadsToDB(void* readsFile)
{
	opt::chastityFilter = false;
	FastaReader fileHandle((const char *)readsFile,
			FastaReader::KEEP_N);
	for (FastaRecord rec; fileHandle >> rec;) {
		const Sequence& seq = rec.seq;
		ostringstream output;
		if (seq.find_first_not_of("ACGT0123") == string::npos) {
			if (opt::colourSpace)
				assert(isdigit(seq[0]));
			else
				assert(isalpha(seq[0]));

			if (opt::multimap)
				g_aligner_m->alignRead(seq,
						prefix_ostream_iterator<Alignment>(
							output, "\t"));
			else
				g_aligner_u->alignRead(seq,
						prefix_ostream_iterator<Alignment>(
							output, "\t"));
		}

		pthread_mutex_lock(&g_mutexCout);
		cout << rec.id;
		if (opt::printSeq) {
			cout << ' ';
			if (opt::colourSpace)
				cout << rec.anchor;
			cout << seq;
		}
		cout << output.str() << '\n';
		assert(cout.good());
		pthread_mutex_unlock(&g_mutexCout);

		if (opt::verbose > 0) {
			pthread_mutex_lock(&g_mutexCerr);
			if (++g_readCount % 1000000 == 0)
				cerr << "Aligned " << g_readCount << " reads\n";
			pthread_mutex_unlock(&g_mutexCerr);
		}
	}
	assert(fileHandle.eof());
	if (opt::threads > 0)
		sem_post(&g_activeThreads);
	return NULL;
}
