#include "config.h"
#include "Common/Options.h"
#include "ContigNode.h"
#include "ContigPath.h"
#include "ContigProperties.h"
#include "DataLayer/Options.h"
#include "Dictionary.h"
#include "FastaReader.h"
#include "IOUtil.h"
#include "MemoryUtil.h"
#include "smith_waterman.h"
#include "StringUtil.h"
#include "Uncompress.h"
#include "Graph/ContigGraph.h"
#include "Graph/DirectedGraph.h"
#include "Graph/GraphIO.h"
#include <algorithm>
#include <cstdlib>
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
"Copyright 2011 Canada's Michael Smith Genome Science Centre\n";

static const char USAGE_MESSAGE[] =
"Usage: " PROGRAM " [OPTION]... FASTA [OVERLAP] PATH\n"
"Merge paths of contigs to create larger contigs.\n"
"  FASTA    contigs in FASTA format\n"
"  OVERLAP  contig overlap graph\n"
"  PATH     sequences of contig IDs\n"
"\n"
"  -k, --kmer=KMER_SIZE  k-mer size\n"
"  -o, --out=FILE        output the merged contigs to FILE [stdout]\n"
"      --merged          output only merged contigs\n"
"  -v, --verbose         display verbose output\n"
"      --help            display this help and exit\n"
"      --version         output version information and exit\n"
"\n"
"Report bugs to <" PACKAGE_BUGREPORT ">.\n";

namespace opt {
	unsigned k; // used by ContigProperties
	static string out = "-";

	/** Output only merged contigs. */
	int onlyMerged;

	/** Minimum overlap. */
	static unsigned minOverlap = 20;

	/** Minimum alignment identity. */
	static float minIdentity = 0.9;
}

static const char shortopts[] = "k:o:v";

enum { OPT_HELP = 1, OPT_VERSION };

static const struct option longopts[] = {
	{ "kmer",        required_argument, NULL, 'k' },
	{ "merged",      no_argument,       &opt::onlyMerged, 1 },
	{ "out",         required_argument, NULL, 'o' },
	{ "path",        required_argument, NULL, 'p' },
	{ "verbose",     no_argument,       NULL, 'v' },
	{ "help",        no_argument,       NULL, OPT_HELP },
	{ "version",     no_argument,       NULL, OPT_VERSION },
	{ NULL, 0, NULL, 0 }
};

/* A contig sequence. */
struct Contig {
	Contig(const string& comment, const string& seq)
		: comment(comment), seq(seq) { }
	Contig(const FastaRecord& o) : comment(o.comment), seq(o.seq) { }
	string comment;
	string seq;
};

/** The contig sequences. */
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
			: ambiguityIsSubset(ca, cb) ? ambiguityOr(ca, cb)
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
		if (!o.empty()) {
			seq.resize(seq.length() - overlap);
			seq += o;
			seq += Sequence(s, overlap);
			return;
		}
	} while (chomp(seq, 'n'));

	// Try an overlap alignment.
	if (opt::verbose > 2)
		cerr << '\n';
	vector<overlap_align> overlaps;
	alignOverlap(ao, bo, 0, overlaps, false, opt::verbose > 2);
	bool good = false;
	if (!overlaps.empty()) {
		assert(overlaps.size() == 1);
		const overlap_align& o = overlaps.front();
		unsigned matches = o.overlap_match;
		const string& consensus = o.overlap_str;
		float identity = (float)matches / consensus.size();
		good = matches >= opt::minOverlap
			&& identity >= opt::minIdentity;
		if (opt::verbose > 2)
			cerr << matches << " / " << consensus.size()
				<< " = " << identity
				<< (matches < opt::minOverlap ? " (too few)"
						: identity < opt::minIdentity ? " (too low)"
						: " (good)") << '\n';
	}
	if (good) {
		assert(overlaps.size() == 1);
		const overlap_align& o = overlaps.front();
		seq.erase(seq.length() - overlap + o.overlap_t_pos);
		seq += o.overlap_str;
		seq += Sequence(s, o.overlap_h_pos + 1);
	} else {
		cerr << "warning: the head of `" << v << "' "
			"does not match the tail of the previous contig\n"
			<< ao << '\n' << bo << '\n' << path << endl;
		seq += 'n';
		seq += s;
	}
}

/** Return a FASTA comment for the specified path. */
static void pathToComment(ostream& out, const ContigPath& path)
{
	assert(path.size() > 1);
	out << path.front();
	if (path.size() == 3)
		out << ',' << path[1];
	else if (path.size() > 3)
		out << ",...";
	out << ',' << path.back();
}

/** Merge the specified path. */
static Contig mergePath(const Graph& g, const Path& path)
{
	Sequence seq;
	unsigned coverage = 0;
	for (Path::const_iterator it = path.begin();
			it != path.end(); ++it) {
		if (!it->ambiguous())
			coverage += g[*it].coverage;
		if (seq.empty()) {
			seq = sequence(*it);
		} else {
			assert(it != path.begin());
			mergeContigs(g, *(it-1), *it, seq, path);
		}
	}
	ostringstream ss;
	ss << seq.size() << ' ' << coverage << ' ';
	pathToComment(ss, path);
	return Contig(ss.str(), seq);
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
		assert_good(fin, inPath);
	istream& in = inPath == "-" ? cin : fin;

	unsigned count = 0;
	vector<Path> paths;
	string id;
	Path path;
	while (in >> id >> path) {
		paths.push_back(path);
		if (ids != NULL)
			ids->push_back(id);

		++count;
		if (opt::verbose > 1 && count % 1000000 == 0)
			cerr << "Read " << count << " paths. "
				"Using " << toSI(getMemoryUsage())
				<< "B of memory.\n";
	}
	if (opt::verbose > 0)
		cerr << "Read " << count << " paths. "
			"Using " << toSI(getMemoryUsage()) << "B of memory.\n";
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

	bool die = false;
	for (int c; (c = getopt_long(argc, argv,
					shortopts, longopts, NULL)) != -1;) {
		istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
			case '?': die = true; break;
			case 'k': arg >> opt::k; break;
			case 'o': arg >> opt::out; break;
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

	if (argc - optind < 2) {
		cerr << PROGRAM ": missing arguments\n";
		die = true;
	}

	if (argc - optind > 3) {
		cerr << PROGRAM ": too many arguments\n";
		die = true;
	}

	if (die) {
		cerr << "Try `" << PROGRAM
			<< " --help' for more information.\n";
		exit(EXIT_FAILURE);
	}

	const char* contigFile = argv[optind++];
	string adjPath, mergedPathFile;
	if (argc - optind > 1)
		adjPath = string(argv[optind++]);
	mergedPathFile = string(argv[optind++]);

	// Read the contig sequence.
	vector<Contig>& contigs = g_contigs;
	{
		if (opt::verbose > 0)
			cerr << "Reading `" << contigFile << "'..." << endl;
		unsigned count = 0;
		FastaReader in(contigFile, FastaReader::NO_FOLD_CASE);
		for (FastaRecord rec; in >> rec;) {
			ContigID id = ContigID::insert(rec.id);
			assert(id == contigs.size());
			contigs.push_back(rec);

			++count;
			if (opt::verbose > 1 && count % 1000000 == 0)
				cerr << "Read " << count << " sequences. "
					"Using " << toSI(getMemoryUsage())
					<< "B of memory.\n";
		}
		if (opt::verbose > 0)
			cerr << "Read " << count << " sequences. "
				"Using " << toSI(getMemoryUsage())
				<< "B of memory.\n";
		assert(in.eof());
		assert(!contigs.empty());
		opt::colourSpace = isdigit(contigs[0].seq[0]);
		ContigID::lock();
	}

	vector<string> pathIDs;
	vector<Path> paths = readPaths(mergedPathFile, &pathIDs);

	// Record all the contigs that are in a path.
	vector<bool> seen(contigs.size());
	seenContigs(seen, paths);
	markRemovedContigs(seen, pathIDs, paths);

	// Output those contigs that were not seen in a path.
	ofstream fout;
	ostream& out = opt::out == "-" ? cout
		: (fout.open(opt::out.c_str()), fout);
	assert_good(out, opt::out);
	if (!opt::onlyMerged) {
		for (vector<Contig>::const_iterator it = contigs.begin();
				it != contigs.end(); ++it) {
			ContigID id(it - contigs.begin());
			if (!seen[id]) {
				const Contig& contig = *it;
				out << '>' << id;
				if (!contig.comment.empty())
					out << ' ' << contig.comment;
				out << '\n' << contig.seq << '\n';
			}
		}
	}

	if (adjPath.empty())
		return 0;

	// Read the contig adjacency graph.
	if (opt::verbose > 0)
		cerr << "Reading `" << adjPath << "'..." << endl;
	ifstream fin(adjPath.c_str());
	assert_good(fin, adjPath);
	Graph g;
	fin >> g;
	assert(fin.eof());
	if (opt::verbose > 0)
		cerr << "Read " << num_vertices(g) << " vertices. "
			"Using " << toSI(getMemoryUsage()) << "B of memory.\n";

	unsigned npaths = 0;
	for (vector<Path>::const_iterator it = paths.begin();
			it != paths.end(); ++it) {
		const Path& path = *it;
		if (path.empty())
			continue;
		Contig contig = mergePath(g, path);
		out << '>' << pathIDs[it - paths.begin()]
			<< ' ' << contig.comment << '\n'
			<< contig.seq << '\n';
		assert_good(out, opt::out);
		npaths++;
	}

	if (npaths == 0)
		return 0;

	float minCov = numeric_limits<float>::infinity(),
		minCovUsed = numeric_limits<float>::infinity();
	for (unsigned i = 0; i < contigs.size(); i++) {
		ContigProperties vp = g[ContigNode(i, false)];
		if (vp.coverage == 0)
			continue;
		assert((int)vp.length - opt::k + 1 > 0);
		float cov = (float)vp.coverage / (vp.length - opt::k + 1);
		minCov = min(minCov, cov);
		if (seen[i])
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
