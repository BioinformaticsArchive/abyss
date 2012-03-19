#include "config.h"
#include "ContigID.h"
#include "ContigLength.h"
#include "ContigPath.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring> // for strerror
#include <cstdlib>
#include <numeric>
#include <string>
#include <sstream>
#include <iostream>
#include <iterator>
#include <fstream>
#include <getopt.h>
#include <map>
#include <set>

using namespace std;

#define PROGRAM "PathOverlap"

static const char *VERSION_MESSAGE =
PROGRAM " (ABySS) " VERSION "\n"
"Written by Tony Raymond.\n"
"\n"
"Copyright 2010 Canada's Michael Smith Genome Science Centre\n";

static const char *USAGE_MESSAGE =
"Usage: " PROGRAM " [OPTION]... LEN PATH\n"
"Find paths that overlap\n"
"  LEN   lengths of the contigs\n"
"  PATH  sequences of contig IDs\n"
"\n"
"  -k, --kmer=KMER_SIZE  k-mer size\n"
"  -r, --repeats=FILE    write repeat contigs to FILE\n"
"      --dot             output overlaps in dot format\n"
"  -v, --verbose         display verbose output\n"
"      --help            display this help and exit\n"
"      --version         output version information and exit\n"
"\n"
"Report bugs to <" PACKAGE_BUGREPORT ">.\n";

namespace opt {
	unsigned k; // used by readContigLengths

	/** Output overlaps in dot format. Do not perform trimming. */
	static int dot;

	/** Output the IDs of contigs in overlaps to this file. */
	static string repeatContigs;

	static int verbose;
}

static const char* shortopts = "k:r:v";

enum { OPT_HELP = 1, OPT_VERSION };

static const struct option longopts[] = {
	{ "kmer",         required_argument, NULL, 'k' },
	{ "dot",          no_argument,       &opt::dot, 1, },
	{ "repeats",      required_argument, NULL, 'r' },
	{ "verbose",      no_argument,       NULL, 'v' },
	{ "help",         no_argument,       NULL, OPT_HELP },
	{ "version",      no_argument,       NULL, OPT_VERSION },
	{ NULL, 0, NULL, 0 }
};

/** Lengths of contigs in k-mer. */
static vector<unsigned> g_contigLengths;

/** Return the length of this contig in k-mer. */
unsigned ContigNode::length() const
{
	return ambiguous() ? m_id : g_contigLengths.at(id());
}

/** The identifiers of the paths. */
static vector<string> g_pathIDs;

/** A vertex of the overlap graph. */
struct Vertex {
	unsigned id;
	bool sense;

	Vertex(unsigned id, bool sense)
		: id(id), sense(sense) { }

	bool operator ==(const Vertex& v) const
	{
		return id == v.id && sense == v.sense;
	}

	friend ostream& operator <<(ostream& out, const Vertex& v)
	{
		return out << '"' << g_pathIDs[v.id]
			<< (v.sense ? '-' : '+') << '"';
	}
};

/** An alignment result. */
struct Overlap {
	Vertex source;
	Vertex target;

	/** Overlap measured in number of contigs. */
	unsigned overlap;

	/** Overlap measured in bp. */
	int distance;

	Overlap(const Vertex& source, const Vertex& target,
			unsigned overlap, int distance)
		: source(source), target(target),
		overlap(overlap), distance(distance) { }

	friend ostream& operator <<(ostream& out, Overlap o)
	{
		return out << o.source << " -> " << o.target
			<< " [d=" << o.distance << "]";
	}
};

/** The contig IDs that have been removed from paths. */
static set<ContigID> s_trimmedContigs;

static void assert_open(ifstream& f, const string& p)
{
	if (f.is_open())
		return;
	cerr << p << ": " << strerror(errno) << endl;
	exit(EXIT_FAILURE);
}

typedef vector<ContigPath> Paths;

/** Read contig paths from the specified stream. */
static Paths readPaths(const string& inPath)
{
	ifstream fin(inPath.c_str());
	if (inPath != "-")
		assert_open(fin, inPath);
	istream& in = inPath == "-" ? cin : fin;

	assert(in.good());
	Paths paths;
	string id;
	ContigPath path;
	while (in >> id >> path) {
		g_pathIDs.push_back(id);
		paths.push_back(path);
	}
	assert(in.eof());
	return paths;
}

typedef multimap<ContigNode, Vertex> SeedMap;

/** Index the first and last contig of each path to facilitate finding
 * overlaps between paths. */
static SeedMap makeSeedMap(const Paths& paths)
{
	SeedMap seedMap;
	for (Paths::const_iterator it = paths.begin();
			it != paths.end(); ++it) {
		if (it->empty())
			continue;
		assert(!it->front().ambiguous());
		seedMap.insert(make_pair(it->front(),
					Vertex(it - paths.begin(), false)));
		assert(!it->back().ambiguous());
		seedMap.insert(make_pair(~it->back(),
					Vertex(it - paths.begin(), true)));
	}
	return seedMap;
}

/** Add the number of k-mer in two contigs. */
static unsigned addLength(unsigned addend, const ContigNode& contig)
{
	return addend + contig.length();
}

/** Check whether path starts with the sequence [first, last). */
static bool startsWith(ContigPath path, bool rc,
		ContigPath::const_iterator first,
		ContigPath::const_iterator last)
{
	if (rc)
		path.reverseComplement();
	assert(*first == path.front());
	assert(first < last);
	return unsigned(last - first) > path.size() ? false
		: equal(first, last, path.begin());
}

/** Check whether path starts with the sequence [first, last). */
static unsigned findOverlap(const Paths& paths,
		ContigPath::const_iterator first,
		ContigPath::const_iterator last,
		const Vertex& v, int &distance)
{
	if (!startsWith(paths[v.id], v.sense, first, last))
		return 0;
	distance = -accumulate(first, last, opt::k-1, addLength);
	return last - first;
}

typedef vector<Overlap> Overlaps;

/** Find every path that overlaps with the specified path. */
static void findOverlaps(const Paths& paths, const SeedMap& seedMap,
		const Vertex& v, Overlaps& overlaps)
{
	ContigPath rc;
	if (v.sense) {
		rc = paths[v.id];
		rc.reverseComplement();
	}
	const ContigPath& path = v.sense ? rc : paths[v.id];

	for (ContigPath::const_iterator it = path.begin();
			it != path.end(); ++it) {
		if (it->ambiguous())
			continue;

		pair<SeedMap::const_iterator, SeedMap::const_iterator>
			range = seedMap.equal_range(*it);
		for (SeedMap::const_iterator seed = range.first;
				seed != range.second; ++seed) {
			if (v == seed->second)
				continue;
			int distance = 0;
			unsigned overlap = findOverlap(paths, it, path.end(),
					   seed->second, distance);
			if (overlap > 0)
				overlaps.push_back(Overlap(v, seed->second,
					overlap, distance));

		}
	}
}

/** Find every pair of overlapping paths. */
static Overlaps findOverlaps(const Paths& paths)
{
	SeedMap seedMap = makeSeedMap(paths);

	Overlaps overlaps;
	for (Paths::const_iterator it = paths.begin();
			it != paths.end(); ++it) {
		unsigned i = it - paths.begin();
		findOverlaps(paths, seedMap, Vertex(i, false), overlaps);
		findOverlaps(paths, seedMap, Vertex(i, true), overlaps);
	}
	return overlaps;
}

/** Record the trimmed contigs. */
static void recordTrimmedContigs(
		ContigPath::const_iterator first,
		ContigPath::const_iterator last)
{
	for (ContigPath::const_iterator it = first; it != last; ++it)
		if (!it->ambiguous())
			s_trimmedContigs.insert(ContigID(*it));
}

/** Remove ambiguous contigs from the ends of the path. */
static void removeAmbiguousContigs(ContigPath& path)
{
	if (!path.empty() && path.back().ambiguous())
		path.erase(path.end() - 1);
	if (!path.empty() && path.front().ambiguous())
		path.erase(path.begin());
}

/** Remove the overlapping portion of the specified contig. */
static void removeContigs(ContigPath& path,
		unsigned first, unsigned last)
{
	assert(first <= path.size());
	assert(last <= path.size());
	if (first < last) {
		recordTrimmedContigs(path.begin(), path.begin() + first);
		recordTrimmedContigs(path.begin() + last, path.end());
		path.erase(path.begin() + last, path.end());
		path.erase(path.begin(), path.begin() + first);
	} else {
		recordTrimmedContigs(path.begin(), path.end());
		path.clear();
	}
	removeAmbiguousContigs(path);
}

/** Find the largest overlap for each contig and remove it. */
static void trimOverlaps(Paths& paths, const Overlaps& overlaps)
{
	vector<unsigned> removed[2];
	removed[0].resize(paths.size());
	removed[1].resize(paths.size());

	for (Overlaps::const_iterator it = overlaps.begin();
			it != overlaps.end(); ++it) {
		unsigned& a = removed[!it->source.sense][it->source.id];
		unsigned& b = removed[it->target.sense][it->target.id];
		a = max(a, it->overlap);
		b = max(b, it->overlap);
	}

	for (Paths::iterator it = paths.begin(); it != paths.end(); ++it)
		removeContigs(*it, removed[0][it - paths.begin()],
				it->size() - removed[1][it - paths.begin()]);
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
			case 'r': arg >> opt::repeatContigs; break;
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

	if (argc - optind < 2) {
		cerr << PROGRAM ": missing arguments\n";
		die = true;
	}

	if (die) {
		cerr << "Try `" << PROGRAM
			<< " --help' for more information.\n";
		exit(EXIT_FAILURE);
	}

	g_contigLengths = readContigLengths(argv[optind++]);
	string pathsFile(argv[optind++]);
	Paths paths = readPaths(pathsFile);

	if (opt::dot) {
		Overlaps overlaps = findOverlaps(paths);
		cout << "digraph \"" << pathsFile << "\" {\n";
		copy(overlaps.begin(), overlaps.end(),
				ostream_iterator<Overlap>(cout, "\n"));
		cout << "}\n";
		return 0;
	}

	for (Overlaps overlaps = findOverlaps(paths);
			!overlaps.empty(); overlaps = findOverlaps(paths)) {
		cerr << "Found " << overlaps.size() / 2 << " overlaps.\n";
		trimOverlaps(paths, overlaps);
	}

	for (Paths::const_iterator it = paths.begin();
			it != paths.end(); ++it) {
		if (it->size() < 2)
			continue;
		cout << g_pathIDs[it - paths.begin()] << '\t' << *it << '\n';
	}

	if (!opt::repeatContigs.empty()) {
		ofstream out(opt::repeatContigs.c_str());
		for (set<ContigID>::const_iterator it
				= s_trimmedContigs.begin();
				it != s_trimmedContigs.end(); ++it)
			out << ContigID(*it) << '\n';
		assert(out.good());
	}

	return 0;
}
