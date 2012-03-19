#include "config.h"
#include "Common/Options.h"
#include "Dictionary.h"
#include "FastaReader.h"
#include <algorithm>
#include <cstdlib>
#include <cerrno>
#include <cstring> // for strerror
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

#define PROGRAM "MergeContigs"

static const char VERSION_MESSAGE[] =
PROGRAM " (" PACKAGE_NAME ") " VERSION "\n"
"Written by Shaun Jackman.\n"
"\n"
"Copyright 2009 Canada's Michael Smith Genome Science Centre\n";

static const char USAGE_MESSAGE[] =
"Usage: " PROGRAM " [OPTION]... CONTIG PATH\n"
"Merge paths of contigs to create larger contigs.\n"
"  CONTIG  contigs in FASTA format\n"
"  PATH    paths of these contigs\n"
"\n"
"  -f, --finish          removes contigs used in previous paths\n"
"  -k, --kmer=KMER_SIZE  k-mer size\n"
"  -o, --out=FILE        write result to FILE\n"
"  -p, --path=PATH_FILE  paths output by SimpleGraph\n"
"  -v, --verbose         display verbose output\n"
"      --help            display this help and exit\n"
"      --version         output version information and exit\n"
"\n"
"Report bugs to <" PACKAGE_BUGREPORT ">.\n";

namespace opt {
	static unsigned k;
	static string out;
	static string path;
	static bool finish;
}

static const char shortopts[] = "fk:o:p:v";

enum { OPT_HELP = 1, OPT_VERSION };

static const struct option longopts[] = {
	{ "finish",      no_argument,       NULL, 'f' },
	{ "kmer",        required_argument, NULL, 'k' },
	{ "out",         required_argument, NULL, 'o' },
	{ "path",        required_argument, NULL, 'p' },
	{ "verbose",     no_argument,       NULL, 'v' },
	{ "help",        no_argument,       NULL, OPT_HELP },
	{ "version",     no_argument,       NULL, OPT_VERSION },
	{ NULL, 0, NULL, 0 }
};

/** Return the last character of s and remove it. */
static char chop(string& s)
{
	assert(s.length() > 1);
	unsigned back = s.length() - 1;
	char c = s[back];
	s.erase(back);
	return c;
}

static unsigned line_num;

static void assert_plus_minus(char c)
{
	if (c != '+' && c != '-') {
		cerr << "error: " << line_num
			<< ": expected `+' or `-' and saw `" << c << '\''
			<< endl;
		exit(EXIT_FAILURE);
	}
}

static Dictionary g_dict;

struct ContigNode {
	unsigned id;
	bool sense;

	friend istream& operator >>(istream& in, ContigNode& o)
	{
		string s;
		if (in >> s) {
			char c = chop(s);
			o.id = g_dict.serial(s);
			assert_plus_minus(c);
			o.sense = c == '-';
		}
		return in;
	}

	friend ostream& operator <<(ostream& out, const ContigNode& o)
	{
		return out << g_dict.key(o.id) << (o.sense ? '-' : '+');
	}
};

struct Path : vector<ContigNode>
{
	friend ostream& operator <<(ostream& out, const Path& o)
	{
		copy(o.begin(), o.end()-1,
				ostream_iterator<ContigNode>(out, ","));
		return out << o.back();
	}
};

static void assert_open(ifstream& f, const string& p)
{
	if (f.is_open())
		return;
	cerr << p << ": " << strerror(errno) << endl;
	exit(EXIT_FAILURE);
}

struct Contig {
	string id;
    Sequence seq;
    unsigned coverage;
    Contig(const string& id, const Sequence& seq, unsigned coverage)
        : id(id), seq(seq), coverage(coverage) { }

	operator FastaRecord()
	{
		ostringstream s;
		s << seq.length() << ' ' << coverage;
		return FastaRecord(id, s.str(), seq);
	}

	friend ostream& operator <<(ostream& out, const Contig& o)
	{
		return out << '>' << o.id << ' '
			<< o.seq.length() << ' ' << o.coverage << '\n'
			<< o.seq << '\n';
	}
};

static Contig mergePath(const Path& path,
		const vector<Contig>& contigs)
{
	Sequence seq;
	unsigned coverage = 0;
	for (Path::const_iterator it = path.begin();
			it != path.end(); ++it) {
		const Contig& contig = contigs[it->id];
		coverage += contig.coverage;

		Sequence h = it->sense ? reverseComplement(contig.seq)
			: contig.seq;
		if (seq.empty()) {
			seq = h;
			continue;
		}

		unsigned overlap = opt::k - 1;
		Sequence a(seq, 0, seq.length() - overlap);
		Sequence ao(seq, seq.length() - overlap);
		Sequence bo(h, 0, overlap);
		Sequence b(h, overlap);
		if (ao != bo) {
			cerr << "error: the head of `" << contig.id << "' "
				"does not match the tail of the previous contig\n"
				<< ao << '\n' << bo << '\n' << path << endl;
			exit(EXIT_FAILURE);
		}
		seq += b;
	}
	return Contig("", seq, coverage);
}

template<typename T> static string toString(T x)
{
	ostringstream s;
	s << x;
	return s.str();
}

/** Loads all paths from the file named inPath into paths. */
static void loadPaths(string& inPath, vector<Path> paths)
{
	ifstream fin(inPath.c_str());
	if (opt::verbose > 0)
		cerr << "Reading `" << inPath << "'..." << endl;
	if (inPath != "-")
		assert_open(fin, inPath);
	istream& in = inPath == "-" ? cin : fin;

	for (string s; getline(in, s);) {
		line_num++;
		istringstream ss(s);
		Path path;
		copy(istream_iterator<ContigNode>(ss),
				istream_iterator<ContigNode>(),
				back_inserter(path));
		paths.push_back(path);
	}
	assert(in.eof());
}

/** Finds all contigs used in each path in paths, and
 * marks them as seen in the vector seen. */
static void seenContigs(vector<bool>& seen, const vector<Path>& paths)
{
	for (vector<Path>::const_iterator it = paths.begin();
			it != paths.end(); ++it)
		for (Path::const_iterator itc = it->begin();
				itc != it->end(); ++itc)
			if (itc->id < seen.size())
				seen[itc->id] = true;
}

int main(int argc, char** argv)
{
	bool die = false;
	for (int c; (c = getopt_long(argc, argv,
					shortopts, longopts, NULL)) != -1;) {
		istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
			case '?': die = true; break;
			case 'f': opt::finish = true; break;
			case 'k': arg >> opt::k; break;
			case 'o': arg >> opt::out; break;
			case 'p': arg >> opt::path; break;
			case 'v': opt::verbose++; break;
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

	if (opt::out.empty()) {
		cerr << PROGRAM ": " << "missing -o,--out option\n";
		die = true;
	}

	if (opt::path.empty()) {
		cerr << PROGRAM ": " << "missing -p,--path option\n";
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

	const char* contigFile = argv[optind++];
	string mergedPathFile(argv[optind++]);

	vector<Contig> contigs;
	{
		FastaReader in(contigFile, FastaReader::KEEP_N);
		for (FastaRecord rec; in >> rec;) {
			istringstream ss(rec.comment);
			unsigned length, coverage = 0;
			ss >> length >> coverage;
			unsigned serial = g_dict.serial(rec.id);
			assert(contigs.size() == serial);
			(void)serial;
			contigs.push_back(Contig(rec.id, rec.seq, coverage));
		}
		assert(in.eof());
		assert(!contigs.empty());
		opt::colourSpace = isdigit(contigs[0].seq[0]);
		if (argc - optind == 0) g_dict.lock();
	}

	vector<Path> paths;
	loadPaths(mergedPathFile, paths); 
	if (opt::verbose > 0)
		cerr << "Total number of paths: " << paths.size() << '\n';

	vector<Path> prevPaths;
	for (;optind < argc; optind++) {
		string filename(argv[optind]);
		loadPaths(filename, prevPaths);
	}
	if (opt::verbose > 0)
		cerr << "Total number of old paths: " << prevPaths.size() << '\n';

	ofstream out(opt::out.c_str());
	assert(out.good());

	// Record all the contigs that were seen in a path.
	vector<bool> seen(contigs.size());
	seenContigs(seen, paths);
	seenContigs(seen, prevPaths);

	vector<bool> seenPivots(contigs.size());
	{
		ifstream fin(opt::path.c_str());
		for (string s; getline(fin, s);) {
			char at = 0;
			line_num++;
			istringstream ss(s);
			string pivot;
			ss >> at >> pivot;
			assert(at == '@');
			char sense = chop(pivot);
			assert(sense == '0' || sense == '1');
			(void)sense;
			char comma = chop(pivot);
			assert(comma == ',');
			(void)comma;
			unsigned pivotNum = g_dict.serial(pivot);
			assert(pivotNum < contigs.size());
			// Only count a pivot as seen if it was seen in a final path
			if (seen[pivotNum]) seenPivots[pivotNum] = true;
		}
		assert(fin.eof());
	}

	// Output those contigs that were not seen in a path.
	for (vector<Contig>::const_iterator it = contigs.begin();
			it != contigs.end(); ++it)
		if ((opt::finish && !seen[g_dict.serial(it->id)]) ||
				(!opt::finish && !seenPivots[g_dict.serial(it->id)]))
			out << *it;

	int id;
	stringstream s(g_dict.key(contigs.size() - 1));
	s >> id;
	id++;
	for (vector<Path>::const_iterator it = paths.begin();
			it != paths.end(); ++it) {
		const Path& path = *it;
		Contig contig = mergePath(path, contigs);
		contig.id = toString(id++);
		FastaRecord rec(contig);
		rec.comment += ' ' + toString(path);
		out << rec;
		assert(out.good());
	}

	return 0;
}
