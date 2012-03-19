#include "config.h"
#include "Common/Options.h"
#include "ContigGraph.h"
#include "ContigNode.h"
#include "ContigPath.h"
#include "ContigProperties.h"
#include "DataLayer/Options.h"
#include "Dictionary.h"
#include "DirectedGraph.h"
#include "FastaReader.h"
#include "StringUtil.h"
#include <algorithm>
#include <cstdlib>
#include <cerrno>
#include <cstring> // for strerror
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

#define PROGRAM "MergeContigs"

static const char VERSION_MESSAGE[] =
PROGRAM " (" PACKAGE_NAME ") " VERSION "\n"
"Written by Shaun Jackman.\n"
"\n"
"Copyright 2010 Canada's Michael Smith Genome Science Centre\n";

static const char USAGE_MESSAGE[] =
"Usage: " PROGRAM " [OPTION]... FASTA ADJ PATH\n"
"Merge paths of contigs to create larger contigs.\n"
"  FASTA  contigs in FASTA format\n"
"  ADJ    contig adjacency graph\n"
"  PATH   sequences of contig IDs\n"
"\n"
"  -k, --kmer=KMER_SIZE  k-mer size\n"
"  -o, --out=FILE        write result to FILE\n"
"  -p, --path=PATH_FILE  paths output by SimpleGraph\n"
"  -v, --verbose         display verbose output\n"
"      --help            display this help and exit\n"
"      --version         output version information and exit\n"
"\n"
"Report bugs to <" PACKAGE_BUGREPORT ">.\n";

namespace opt {
	unsigned k; // used by ContigProperties
	static string out;
	static string path;
}

static const char shortopts[] = "k:o:p:v";

enum { OPT_HELP = 1, OPT_VERSION };

static const struct option longopts[] = {
	{ "kmer",        required_argument, NULL, 'k' },
	{ "out",         required_argument, NULL, 'o' },
	{ "path",        required_argument, NULL, 'p' },
	{ "verbose",     no_argument,       NULL, 'v' },
	{ "help",        no_argument,       NULL, OPT_HELP },
	{ "version",     no_argument,       NULL, OPT_VERSION },
	{ NULL, 0, NULL, 0 }
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

static vector<Contig> g_contigs;

/** Return the sequence of the specified contig node. The sequence
 * may be ambiguous or reverse complemented.
 */
static Sequence sequence(const ContigNode& id)
{
	if (id.ambiguous()) {
		string s(id.ambiguousSequence());
		if (s.length() < opt::k)
			transform(s.begin(), s.end(), s.begin(), ::tolower);
		return string(opt::k - 1, 'N') + s;
	} else {
		const Sequence& seq = g_contigs[id.id()].seq;
		return id.sense() ? reverseComplement(seq) : seq;
	}
}

/** Return a consensus sequence of a and b.
 * @return an empty string if a consensus could not be found
 */
static string createConsensus(const Sequence& a, const Sequence& b)
{
	assert(a.length() == b.length());
	if (a == b)
		return a;
	string s;
	s.reserve(a.length());
	for (string::const_iterator ita = a.begin(), itb = b.begin();
			ita != a.end(); ++ita, ++itb) {
		bool mask = islower(*ita) || islower(*itb);
		char ca = toupper(*ita), cb = toupper(*itb);
		char c = ca == cb ? ca
			: ca == 'N' ? cb
			: cb == 'N' ? ca
			: 'x';
		if (c == 'x')
			return string("");
		s += mask ? tolower(c) : c;
	}
	return s;
}

typedef ContigGraph<DirectedGraph<ContigProperties, Distance> > Graph;
typedef graph_traits<Graph>::vertex_descriptor vertex_descriptor;
typedef ContigPath Path;

/** Append the sequence of contig v to seq. */
static void mergeContigs(const Graph& g,
		vertex_descriptor u, vertex_descriptor v,
		Sequence& seq, const Path& path)
{
	int d = get(edge_bundle, g, u, v).distance;
	assert(d < 0);
	unsigned overlap = -d;
	const Sequence& s = sequence(v);
	assert(s.length() > overlap);
	Sequence ao;
	Sequence bo(s, 0, overlap);
	Sequence o;
	do {
		assert(seq.length() > overlap);
		ao = seq.substr(seq.length() - overlap);
		o = createConsensus(ao, bo);
	} while (o.empty() && chomp(seq, 'n'));
	if (o.empty()) {
		cerr << "warning: the head of `" << v << "' "
			"does not match the tail of the previous contig\n"
			<< ao << '\n' << bo << '\n' << path << endl;
		seq += 'n';
		seq += s;
	} else {
		seq.resize(seq.length() - overlap);
		seq += o;
		seq += Sequence(s, overlap);
	}
}

static Contig mergePath(const Graph& g, const Path& path)
{
	Sequence seq;
	unsigned coverage = 0;
	for (Path::const_iterator it = path.begin();
			it != path.end(); ++it) {
		if (!it->ambiguous())
			coverage += g_contigs[it->id()].coverage;
		if (seq.empty()) {
			seq = sequence(*it);
		} else {
			assert(it != path.begin());
			mergeContigs(g, *(it-1), *it, seq, path);
		}
	}
	return Contig("", seq, coverage);
}

template<typename T> static string toString(T x)
{
	ostringstream s;
	s << x;
	return s.str();
}

/** Read contig paths from the specified file.
 * @param ids [out] the string ID of the paths
 */
static vector<Path> readPaths(const string& inPath,
		vector<string>* ids = NULL)
{
	if (ids != NULL)
		assert(ids->empty());
	ifstream fin(inPath.c_str());
	if (opt::verbose > 0)
		cerr << "Reading `" << inPath << "'..." << endl;
	if (inPath != "-")
		assert_open(fin, inPath);
	istream& in = inPath == "-" ? cin : fin;

	vector<Path> paths;
	string id;
	Path path;
	while (in >> id >> path) {
		paths.push_back(path);
		if (ids != NULL)
			ids->push_back(id);
	}
	assert(in.eof());
	return paths;
}

/** Finds all contigs used in each path in paths, and
 * marks them as seen in the vector seen. */
static void seenContigs(vector<bool>& seen, const vector<Path>& paths)
{
	for (vector<Path>::const_iterator it = paths.begin();
			it != paths.end(); ++it)
		for (Path::const_iterator itc = it->begin();
				itc != it->end(); ++itc)
			if (itc->id() < seen.size())
				seen[itc->id()] = true;
}

/** Mark contigs for removal. An empty path indicates that a contig
 * should be removed.
 */
static void markRemovedContigs(vector<bool>& marked,
		const vector<string>& pathIDs, const vector<Path>& paths)
{
	for (vector<Path>::const_iterator it = paths.begin();
			it != paths.end(); ++it)
		if (it->empty())
			marked[ContigID(pathIDs[it - paths.begin()])] = true;
}

int main(int argc, char** argv)
{
	opt::trimMasked = false;
	ContigPath::separator = ",";

	bool die = false;
	for (int c; (c = getopt_long(argc, argv,
					shortopts, longopts, NULL)) != -1;) {
		istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
			case '?': die = true; break;
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

	if (argc - optind < 3) {
		cerr << PROGRAM ": missing arguments\n";
		die = true;
	}

	if (die) {
		cerr << "Try `" << PROGRAM
			<< " --help' for more information.\n";
		exit(EXIT_FAILURE);
	}

	const char* contigFile = argv[optind++];
	string adjPath(argv[optind++]);
	string mergedPathFile(argv[optind++]);

	vector<Contig>& contigs = g_contigs;
	{
		FastaReader in(contigFile, FastaReader::NO_FOLD_CASE);
		for (FastaRecord rec; in >> rec;) {
			istringstream ss(rec.comment);
			unsigned length, coverage = 0;
			ss >> length >> coverage;
			ContigID id(rec.id);
			assert(id == contigs.size());
			contigs.push_back(Contig(rec.id, rec.seq, coverage));
		}
		assert(in.eof());
		assert(!contigs.empty());
		opt::colourSpace = isdigit(contigs[0].seq[0]);
		if (optind == argc)
			ContigID::lock();
	}

	// Read the contig adjacency graph.
	ifstream fin(adjPath.c_str());
	assert_open(fin, adjPath);
	Graph g;
	fin >> g;
	assert(fin.eof());

	vector<string> pathIDs;
	vector<Path> paths = readPaths(mergedPathFile, &pathIDs);
	if (opt::verbose > 0)
		cerr << "Total number of paths: " << paths.size() << '\n';

	// Record all the contigs that are in a path.
	vector<bool> seen(contigs.size());
	seenContigs(seen, paths);
	markRemovedContigs(seen, pathIDs, paths);

	// Record all the contigs that were in a previous path.
	if (argc - optind > 0) {
		unsigned count = 0;
		for (; optind < argc; optind++) {
			vector<Path> prevPaths = readPaths(argv[optind]);
			seenContigs(seen, prevPaths);
			count += prevPaths.size();
		}
		if (opt::verbose > 0)
			cerr << "Total number of previous paths: "
				<< count << '\n';
	}

	// Record all the contigs that are seeds.
	if (!opt::path.empty()) {
		vector<bool> seenPivots(contigs.size());
		ifstream fin(opt::path.c_str());
		assert_open(fin, opt::path);
		for (ContigID id; fin >> id;) {
			fin.ignore(numeric_limits<streamsize>::max(), '\n');
			assert(id < contigs.size());
			// Only count a pivot as seen if it was in a final path.
			if (seen[id])
				seenPivots[id] = true;
		}
		assert(fin.eof());
		seen = seenPivots;
	}

	// Output those contigs that were not seen in a path.
	ofstream out(opt::out.c_str());
	assert(out.good());
	for (vector<Contig>::const_iterator it = contigs.begin();
			it != contigs.end(); ++it)
		if (!seen[it - contigs.begin()])
			out << *it;

	unsigned npaths = 0;
	for (vector<Path>::const_iterator it = paths.begin();
			it != paths.end(); ++it) {
		const Path& path = *it;
		if (path.empty())
			continue;
		Contig contig = mergePath(g, path);
		contig.id = pathIDs[it - paths.begin()];
		FastaRecord rec(contig);
		rec.comment += ' ' + toString(path);
		out << rec;
		assert(out.good());
		npaths++;
	}

	if (npaths == 0)
		return 0;

	float minCov = numeric_limits<float>::infinity(),
		minCovUsed = numeric_limits<float>::infinity();
	for (vector<Contig>::const_iterator it = contigs.begin();
			it != contigs.end(); ++it) {
		if (it->coverage == 0)
			continue;
		assert((int)it->seq.length() - opt::k + 1 > 0);
		float cov = (float)it->coverage
			/ (it->seq.length() - opt::k + 1);
		minCov = min(minCov, cov);
		if (seen[it - contigs.begin()])
			minCovUsed = min(minCovUsed, cov);
	}

	cerr << "The minimum coverage of single-end contigs is "
		<< minCov << ".\n"
		<< "The minimum coverage of merged contigs is "
		<< minCovUsed << ".\n";
	if (minCov < minCovUsed)
		cerr << "Consider increasing the coverage threshold "
			"parameter, c, to " << minCovUsed << ".\n";

	return 0;
}
