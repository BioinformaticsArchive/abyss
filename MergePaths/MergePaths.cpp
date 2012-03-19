#include "config.h"
#include "Common/Options.h"
#include "AffixIterator.h"
#include "ContigID.h"
#include "ContigLength.h"
#include "ContigPath.h"
#include "Uncompress.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <climits> // for UINT_MAX
#include <cstdlib>
#include <cstring> // for strerror
#include <deque>
#include <fstream>
#include <functional>
#include <getopt.h>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#if _OPENMP
# include <omp.h>
#endif

using namespace std;

#define PROGRAM "MergePaths"

static const char VERSION_MESSAGE[] =
PROGRAM " (" PACKAGE_NAME ") " VERSION "\n"
"Written by Jared Simpson and Shaun Jackman.\n"
"\n"
"Copyright 2010 Canada's Michael Smith Genome Science Centre\n";

static const char USAGE_MESSAGE[] =
"Usage: " PROGRAM " [OPTION]... LEN PATH\n"
"Merge sequences of contigs IDs.\n"
"  LEN   lengths of the contigs\n"
"  PATH  sequences of contig IDs\n"
"\n"
"  -k, --kmer=KMER_SIZE  k-mer size\n"
"  -o, --out=FILE        write result to FILE\n"
"  -j, --threads=N       use N parallel threads [1]\n"
"  -v, --verbose         display verbose output\n"
"      --help            display this help and exit\n"
"      --version         output version information and exit\n"
"\n"
"Report bugs to <" PACKAGE_BUGREPORT ">.\n";

namespace opt {
	unsigned k; // used by readContigLengths
	static string out;
	static int threads = 1;
}

static const char shortopts[] = "j:k:o:v";

enum { OPT_HELP = 1, OPT_VERSION };

static const struct option longopts[] = {
	{ "kmer",        required_argument, NULL, 'k' },
	{ "out",         required_argument, NULL, 'o' },
	{ "threads",     required_argument,	NULL, 'j' },
	{ "verbose",     no_argument,       NULL, 'v' },
	{ "help",        no_argument,       NULL, OPT_HELP },
	{ "version",     no_argument,       NULL, OPT_VERSION },
	{ NULL, 0, NULL, 0 }
};

typedef map<LinearNumKey, ContigPath> ContigPathMap;

/** Lengths of contigs in k-mer. */
static vector<unsigned> g_contigLengths;

/** Return the length of this contig in k-mer. */
unsigned ContigNode::length() const
{
	return ambiguous() ? m_id : g_contigLengths.at(id());
}

static ContigPath align(const ContigPath& p1, const ContigPath& p2,
		const ContigNode& pivot);

static bool gDebugPrint;

/** Return all contigs that are tandem repeats, identified as those
 * contigs that appear more than once in a single path.
 */
static set<LinearNumKey> findRepeats(const ContigPathMap& paths)
{
	set<LinearNumKey> repeats;
	for (ContigPathMap::const_iterator pathIt = paths.begin();
			pathIt != paths.end(); ++pathIt) {
		const ContigPath& path = pathIt->second;
		map<LinearNumKey, unsigned> count;
		for (ContigPath::const_iterator it = path.begin();
				it != path.end(); ++it)
			if (!it->ambiguous())
				count[it->id()]++;
		for (map<LinearNumKey, unsigned>::const_iterator
				it = count.begin(); it != count.end(); ++it)
			if (it->second > 1)
				repeats.insert(it->first);
	}
	return repeats;
}

/** Remove tandem repeats from the set of paths.
 * @return the removed paths
 */
static set<LinearNumKey> removeRepeats(ContigPathMap& paths)
{
	set<LinearNumKey> repeats = findRepeats(paths);
	if (gDebugPrint) {
		cout << "Repeats:";
		if (!repeats.empty())
			transform(repeats.begin(), repeats.end(),
					affix_ostream_iterator<string>(cout, " "),
					idToString);
		else
			cout << " none";
		cout << '\n';
	}

	ostringstream ss;
	unsigned removed = 0;
	for (set<LinearNumKey>::const_iterator it = repeats.begin();
			it != repeats.end(); ++it) {
		if (paths.erase(*it) > 0) {
			ss << ' ' << ContigID(*it);
			removed++;
		}
	}
	if (opt::verbose > 0 && removed > 0)
		cout << "Removing paths in repeats:" << ss.str() << '\n';
	return repeats;
}

static void appendToMergeQ(deque<ContigNode>& mergeQ,
	set<ContigNode>& seen, const ContigPath& path)
{
	for (ContigPath::const_iterator it = path.begin();
			it != path.end(); ++it)
		if (seen.insert(*it).second)
			mergeQ.push_back(*it);
}

/** Attempt to merge the paths specified in mergeQ with path.
 * @return the number of paths merged
 */
static unsigned mergePaths(ContigPath& path,
		deque<ContigNode>& mergeQ, set<ContigNode>& seen,
		const ContigPathMap& paths)
{
	unsigned merged = 0;
	deque<ContigNode> invalid;
	for (ContigNode pivot; !mergeQ.empty(); mergeQ.pop_front()) {
		pivot = mergeQ.front();
		ContigPathMap::const_iterator path2It
			= paths.find(pivot.id());
		if (path2It == paths.end())
			continue;

		ContigPath path2 = path2It->second;
		if (pivot.sense())
			path2.reverseComplement();
		ContigPath consensus = align(path, path2, pivot);
		if (consensus.empty()) {
			invalid.push_back(pivot);
			continue;
		}

		appendToMergeQ(mergeQ, seen, path2);
		path.swap(consensus);
		if (gDebugPrint)
#pragma omp critical(cout)
			cout << pivot << '\t' << path2 << '\n'
				<< '\t' << path << '\n';
		merged++;
	}
	mergeQ.swap(invalid);
	return merged;
}

/** Extend the specified path as long as is unambiguously possible and
 * add the result to the specified container.
 */
static void extendPaths(LinearNumKey id,
		const ContigPathMap& paths, ContigPathMap& out)
{
	ContigPathMap::const_iterator pathIt = paths.find(id);
	assert(pathIt != paths.end());
	pair<ContigPathMap::iterator, bool> inserted;
	#pragma omp critical(out)
	inserted = out.insert(*pathIt);
	assert(inserted.second);
	ContigPath& path = inserted.first->second;

	if (gDebugPrint)
		#pragma omp critical(cout)
		cout << "\n* " << ContigNode(id, false) << '\n'
			<< '\t' << path << '\n';

	set<ContigNode> seen;
	seen.insert(ContigNode(id, false));
	deque<ContigNode> mergeQ;
	appendToMergeQ(mergeQ, seen, path);
	while (mergePaths(path, mergeQ, seen, paths) > 0)
		;

	if (!mergeQ.empty() && gDebugPrint) {
		#pragma omp critical(cout)
		{
			cout << "invalid\n";
			for (deque<ContigNode>::const_iterator it
					= mergeQ.begin(); it != mergeQ.end(); ++it)
				cout << *it << '\t'
					<< paths.find(it->id())->second << '\n';
		}
	}
}

/** Return true if the contigs are equal or both are ambiguous. */
static bool equalOrBothAmbiguos(const ContigNode& a,
		const ContigNode& b)
{
	return a == b || (a.ambiguous() && b.ambiguous());
}

/** Return true if both paths are equal, ignoring ambiguous nodes. */
static bool equalIgnoreAmbiguos(const ContigPath& a,
		const ContigPath& b)
{
	return a.size() == b.size()
		&& equal(a.begin(), a.end(), b.begin(), equalOrBothAmbiguos);
}

/** Return whether this path is a cycle. */
static bool isCycle(const ContigPath& path)
{
	return !align(path, path, path.front()).empty();
}

/** Identify paths subsumed by the specified path.
 * @param overlaps [out] paths that are found to overlap
 * @return the ID of the subsuming path
 */
static LinearNumKey identifySubsumedPaths(
		ContigPathMap::const_iterator path1It,
		ContigPathMap& paths,
		vector<ContigPathMap::iterator>& out,
		set<LinearNumKey>& overlaps)
{
	ostringstream vout;
	out.clear();
	LinearNumKey id = path1It->first;
	const ContigPath& path = path1It->second;
	if (gDebugPrint)
		vout << ContigNode(id, false) << '\t' << path << '\n';

	for (ContigPath::const_iterator it = path.begin();
			it != path.end(); ++it) {
		ContigNode pivot = *it;
		if (pivot.id() == id)
			continue;
		ContigPathMap::iterator path2It = paths.find(pivot.id());
		if (path2It == paths.end())
			continue;
		ContigPath path2 = path2It->second;
		if (pivot.sense())
			path2.reverseComplement();
		ContigPath consensus = align(path, path2, pivot);
		if (consensus.empty())
			continue;
		if (equalIgnoreAmbiguos(consensus, path)) {
			if (gDebugPrint)
				vout << pivot << '\t' << path2 << '\n';
			out.push_back(path2It);
		} else if (equalIgnoreAmbiguos(consensus, path2)) {
			// This path is larger. Use it as the seed.
			return identifySubsumedPaths(path2It, paths, out,
					overlaps);
		} else if (isCycle(consensus)) {
			// The consensus path is a cycle.
			bool isCyclePath1 = isCycle(path);
			bool isCyclePath2 = isCycle(path2);
			if (!isCyclePath1 && !isCyclePath2) {
				// Neither path is a cycle.
				if (gDebugPrint)
					vout << pivot << '\t' << path2 << '\n'
						<< "ignored\t" << consensus << '\n';
				overlaps.insert(id);
				overlaps.insert(path2It->first);
			} else {
				// At least one path is a cycle.
				if (gDebugPrint)
					vout << pivot << '\t' << path2 << '\n'
						<< "cycle\t" << consensus << '\n';
				if (isCyclePath1 && isCyclePath2)
					out.push_back(path2It);
				else if (!isCyclePath1)
					overlaps.insert(id);
				else if (!isCyclePath2)
					overlaps.insert(path2It->first);
			}
		} else {
			if (gDebugPrint)
				vout << pivot << '\t' << path2 << '\n'
					<< "ignored\t" << consensus << '\n';
			overlaps.insert(id);
			overlaps.insert(path2It->first);
		}
	}
	cout << vout.str();
	return id;
}

/** Remove paths subsumed by the specified path.
 * @param seed [out] the ID of the subsuming path
 * @param overlaps [out] paths that are found to overlap
 * @return the next iterator after path1it
 */
static ContigPathMap::const_iterator removeSubsumedPaths(
		ContigPathMap::const_iterator path1It, ContigPathMap& paths,
		LinearNumKey& seed, set<LinearNumKey>& overlaps)
{
	if (gDebugPrint)
		cout << '\n';
	vector<ContigPathMap::iterator> eq;
	seed = identifySubsumedPaths(path1It, paths, eq, overlaps);
	++path1It;
	for (vector<ContigPathMap::iterator>::const_iterator
			it = eq.begin(); it != eq.end(); ++it) {
		if (*it == path1It)
			++path1It;
		paths.erase(*it);
	}
	return path1It;
}

/** Remove paths subsumed by another path.
 * @return paths that are found to overlap
 */
static set<LinearNumKey> removeSubsumedPaths(ContigPathMap& paths)
{
	set<LinearNumKey> overlaps, seen;
	for (ContigPathMap::const_iterator iter = paths.begin();
			iter != paths.end();) {
		if (seen.count(iter->first) == 0) {
			LinearNumKey seed;
			iter = removeSubsumedPaths(iter, paths, seed, overlaps);
			seen.insert(seed);
		} else
			++iter;
	}
	return overlaps;
}

static void assert_open(ifstream& f, const string& p)
{
	if (f.is_open())
		return;
	cerr << p << ": " << strerror(errno) << endl;
	exit(EXIT_FAILURE);
}

/** Read a set of paths from the specified file. */
static ContigPathMap readPaths(const string& filePath)
{
	ifstream in(filePath.c_str());
	assert_open(in, filePath);

	ContigPathMap paths;
	ContigID id;
	ContigPath path;
	while (in >> id >> path) {
		bool inserted = paths.insert(
				make_pair(id, path)).second;
		assert(inserted);
		(void)inserted;
	}
	assert(in.eof());
	return paths;
}

#if _OPENMP
/** Store it in out and increment it.
 * @return true if out != last
 */
template<class T>
bool atomicInc(T& it, T last, T& out)
{
	#pragma omp critical(atomicInc)
	out = it == last ? it : it++;
	return out != last;
}
#endif

int main(int argc, char** argv)
{
	bool die = false;
	for (int c; (c = getopt_long(argc, argv,
					shortopts, longopts, NULL)) != -1;) {
		istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
			case '?': die = true; break;
			case 'j': arg >> opt::threads; break;
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

	if (argc - optind < 2) {
		cerr << PROGRAM ": missing arguments\n";
		die = true;
	} else if (argc - optind > 2) {
		cerr << PROGRAM ": too many arguments\n";
		die = true;
	}

	if (die) {
		cerr << "Try `" << PROGRAM
			<< " --help' for more information.\n";
		exit(EXIT_FAILURE);
	}

	gDebugPrint = opt::verbose > 1;

#if _OPENMP
	if (opt::threads > 0)
		omp_set_num_threads(opt::threads);
#endif

	g_contigLengths = readContigLengths(argv[optind++]);
	ContigPathMap originalPathMap = readPaths(argv[optind++]);

	removeRepeats(originalPathMap);

	ContigPathMap resultsPathMap;
#if _OPENMP
	ContigPathMap::iterator sharedIt = originalPathMap.begin();
	#pragma omp parallel
	for (ContigPathMap::iterator it;
			atomicInc(sharedIt, originalPathMap.end(), it);)
		extendPaths(it->first, originalPathMap, resultsPathMap);
#else
	for (ContigPathMap::const_iterator iter = originalPathMap.begin();
			iter != originalPathMap.end(); ++iter)
		extendPaths(iter->first, originalPathMap, resultsPathMap);
#endif
	if (gDebugPrint)
		cout << '\n';

	set<LinearNumKey> repeats = removeRepeats(resultsPathMap);

	if (gDebugPrint)
		cout << "\nRemoving redundant contigs\n";
	set<LinearNumKey> overlaps = removeSubsumedPaths(resultsPathMap);

	if (!overlaps.empty() && !repeats.empty()) {
		// Remove the newly-discovered repeat contigs from the
		// original paths.
		for (set<LinearNumKey>::const_iterator it = repeats.begin();
				it != repeats.end(); ++it)
			originalPathMap.erase(*it);

		// Reassemble the paths that were found to overlap.
		if (gDebugPrint) {
			cout << "\nReassembling overlapping contigs:";
			transform(overlaps.begin(), overlaps.end(),
					affix_ostream_iterator<string>(cout, " "),
					idToString);
			cout << '\n';
		}

		for (set<LinearNumKey>::const_iterator it = overlaps.begin();
				it != overlaps.end(); ++it) {
			if (originalPathMap.count(*it) == 0)
				continue; // repeat
			ContigPathMap::iterator oldIt = resultsPathMap.find(*it);
			if (oldIt == resultsPathMap.end())
				continue; // subsumed
			ContigPath old = oldIt->second;
			resultsPathMap.erase(oldIt);
			extendPaths(*it, originalPathMap, resultsPathMap);
			if (gDebugPrint) {
				if (resultsPathMap[*it] == old)
					cout << "no change\n";
				else
					cout << "was\t" << old << '\n';
			}
		}
		if (gDebugPrint)
			cout << '\n';

		removeRepeats(resultsPathMap);
		overlaps = removeSubsumedPaths(resultsPathMap);
		if (!overlaps.empty() && gDebugPrint) {
			cout << "\nOverlapping contigs:";
			transform(overlaps.begin(), overlaps.end(),
					affix_ostream_iterator<string>(cout, " "),
					idToString);
			cout << '\n';
		}
	}
	originalPathMap.clear();

	vector<ContigPath> uniquePaths;
	uniquePaths.reserve(resultsPathMap.size());
	for (ContigPathMap::const_iterator it = resultsPathMap.begin();
			it != resultsPathMap.end(); ++it)
		uniquePaths.push_back(it->second);
	sort(uniquePaths.begin(), uniquePaths.end());

	int id;
	istringstream ss(ContigID(g_contigLengths.size() - 1).str());
	ss >> id;
	id++;

	ofstream fout(opt::out.c_str());
	ostream& out = opt::out.empty() ? cout : fout;
	assert(out.good());
	for (vector<ContigPath>::const_iterator it
				= uniquePaths.begin();
			it != uniquePaths.end(); ++it)
		out << id++ << '\t' << *it << '\n';
	assert(out.good());
	return 0;
}

/** Add the number of k-mer in two contigs. */
static unsigned addLength(unsigned addend, const ContigNode& contig)
{
	return addend + contig.length();
}

/** Attempt to fill in gaps in one path with the sequence from the
 * other path and store the consensus at result if an alignment is
 * found.
 * @return true if an alignment is found
 */
template <class iterator, class oiterator>
static bool alignCoordinates(iterator& first1, iterator last1,
		iterator& first2, iterator last2, oiterator& result)
{
	oiterator out = result;

	int ambiguous1 = 0, ambiguous2 = 0;
	iterator it1 = first1, it2 = first2;
	while (it1 != last1 && it2 != last2) {
		if (it1->ambiguous()) {
			ambiguous1 += it1->length();
			++it1;
			assert(it1 != last1);
			assert(!it1->ambiguous());
		}
		if (it2->ambiguous()) {
			ambiguous2 += it2->length();
			++it2;
			assert(it2 != last2);
			assert(!it2->ambiguous());
		}

		if (ambiguous1 > 0 && ambiguous2 > 0) {
			if (ambiguous1 > ambiguous2) {
				*out++ = ContigNode(ambiguous2, 'N');
				ambiguous1 -= ambiguous2;
				ambiguous2 = 0;
			} else {
				*out++ = ContigNode(ambiguous1, 'N');
				ambiguous2 -= ambiguous1;
				ambiguous1 = 0;
			}
		} else if (ambiguous1 > 0) {
			ambiguous1 -= it2->length();
			*out++ = *it2++;
		} else if (ambiguous2 > 0) {
			ambiguous2 -= it1->length();
			*out++ = *it1++;
		} else
			break;
		if (ambiguous1 < 0 || ambiguous2 < 0)
			return false;
	}

	assert(ambiguous1 == 0 || ambiguous2 == 0);
	unsigned ambiguous = ambiguous1 + ambiguous2;
	if (ambiguous > 0)
		*out++ = ContigNode(ambiguous, 'N');
	first1 = it1;
	first2 = it2;
	result = out;
	return true;
}

/** Align the ambiguous region [it1, it1e) to [it2, it2e) and store
 * the consensus at out if an alignment is found.
 * @return true if an alignment is found
 */
template <class iterator, class oiterator>
static bool buildConsensus(iterator it1, iterator it1e,
		iterator it2, iterator it2e, oiterator& out)
{
	iterator it1b = it1 + 1;
	assert(!it1b->ambiguous());

	if (it1b == it1e) {
		// path2 completely fills the gap in path1.
		out = copy(it2, it2e, out);
		return true;
	}

	// The gaps of path1 and path2 overlap.
	iterator it2a = it2e - 1;
	if (it2e == it2 || !it2a->ambiguous()) {
		// The two paths do not agree. No alignment.
		return false;
	}

	unsigned ambiguous1 = it1->length();
	unsigned ambiguous2 = it2a->length();
	unsigned unambiguous1 = accumulate(it1b, it1e,
			0, addLength);
	unsigned unambiguous2 = accumulate(it2, it2a,
			0, addLength);
	if (ambiguous1 < unambiguous2
			|| ambiguous2 < unambiguous1) {
		// Two gaps overlap and either of the gaps is smaller
		// than the unambiguous sequence that overlaps the
		// gap. No alignment.
		return false;
	}

	unsigned n = min(ambiguous2 - unambiguous1,
			ambiguous1 - unambiguous2);

	out = copy(it2, it2a, out);
	if (n > 0)
		*out++ = ContigNode(n, 'N');
	out = copy(it1b, it1e, out);
	return true;
}

/** Align the ambiguous region [it1, last1) to [it2, last2) using it1e
 * as the seed of the alignment. The end of the alignment is returned
 * in it1 and it2.
 * @return true if an alignment is found
 */
template <class iterator, class oiterator>
static bool alignAtSeed(
		iterator& it1, iterator it1e, iterator last1,
		iterator& it2, iterator last2, oiterator& out)
{
	assert(it1 != last1);
	assert(it1->ambiguous());
	assert(it1 + 1 != last1);
	assert(!it1e->ambiguous());
	assert(it2 != last2);

	// Find the best seeded alignment. The best alignment has the
	// fewest number of contigs in the consensus sequence.
	unsigned bestLen = UINT_MAX;
	iterator bestIt2e;
	for (iterator it2e = it2;
			(it2e = find(it2e, last2, *it1e)) != last2; ++it2e) {
		oiterator myOut = out;
		if (buildConsensus(it1, it1e, it2, it2e, myOut)
				&& align(it1e, last1, it2e, last2, myOut)) {
			unsigned len = myOut - out;
			if (len <= bestLen) {
				bestLen = len;
				bestIt2e = it2e;
			}
		}
	}
	if (bestLen != UINT_MAX) {
		bool good = buildConsensus(it1, it1e, it2, bestIt2e, out)
				&& align(it1e, last1, bestIt2e, last2, out);
		assert(good);
		it1 = last1;
		it2 = last2;
		return good;
	} else
		return false;
}

/** Align the ambiguous region [it1, last1) to [it2, last2).
 * The end of the alignment is returned in it1 and it2.
 * @return true if an alignment is found
 */
template <class iterator, class oiterator>
static bool alignAmbiguous(iterator& it1, iterator last1,
		iterator& it2, iterator last2, oiterator& out)
{
	assert(it1 != last1);
	assert(it1->ambiguous());
	assert(it1 + 1 != last1);
	assert(it2 != last2);

	// Find a seed for the alignment.
	for (iterator it1e = it1; it1e != last1; ++it1e) {
		if (it1e->ambiguous())
			continue;
		if (alignAtSeed(it1, it1e, last1, it2, last2, out))
			return true;
	}

	// No valid seeded alignment. Check whether path2 fits entirely
	// within the gap of path1.
	return alignCoordinates(it1, last1, it2, last2, out);
}

/** Align the next pair of contigs.
 * The end of the alignment is returned in it1 and it2.
 * @return true if an alignment is found
 */
template <class iterator, class oiterator>
static bool alignOne(iterator& it1, iterator last1,
		iterator& it2, iterator last2, oiterator& out)
{
	return
		it1->ambiguous() && it2->ambiguous()
			? (it1->length() > it2->length()
				? alignAmbiguous(it1, last1, it2, last2, out)
				: alignAmbiguous(it2, last2, it1, last1, out))
		: it1->ambiguous()
			? alignAmbiguous(it1, last1, it2, last2, out)
		: it2->ambiguous()
			? alignAmbiguous(it2, last2, it1, last1, out)
			: (*out++ = *it1, *it1++ == *it2++);
}

/** Align the ambiguous region [it1, last1) to [it2, last2)
 * and store the consensus at out if an alignment is found.
 * @return true if an alignment is found
 */
template <class iterator, class oiterator>
static bool align(iterator it1, iterator last1,
		iterator it2, iterator last2, oiterator& out)
{
	assert(it1 != last1);
	assert(it2 != last2);
	while (it1 != last1 && it2 != last2)
		if (!alignOne(it1, last1, it2, last2, out))
			return false;
	assert(it1 == last1 || it2 == last2);
	out = copy(it1, last1, out);
	out = copy(it2, last2, out);
	return true;
}

/** Find an equivalent region of the two specified paths, starting the
 * alignment at pivot1 of path1 and pivot2 of path2.
 * @return the consensus sequence
 */
static ContigPath align(const ContigPath& p1, const ContigPath& p2,
		ContigPath::const_iterator pivot1,
		ContigPath::const_iterator pivot2)
{
	ContigPath::const_reverse_iterator
		rit1 = ContigPath::const_reverse_iterator(pivot1+1),
		rit2 = ContigPath::const_reverse_iterator(pivot2+1);
	ContigPath alignmentr(p1.rend() - rit1 + p2.rend() - rit2);
	ContigPath::iterator rout = alignmentr.begin();
	bool alignedr = align(rit1, p1.rend(), rit2, p2.rend(), rout);
	alignmentr.erase(rout, alignmentr.end());

	ContigPath alignmentf(p1.end() - pivot1 + p2.end() - pivot2);
	ContigPath::iterator fout = alignmentf.begin();
	bool alignedf = align(pivot1, p1.end(), pivot2, p2.end(), fout);
	alignmentf.erase(fout, alignmentf.end());

	ContigPath consensus;
	if (alignedr && alignedf) {
		// Found an alignment.
		assert(!alignmentf.empty());
		assert(!alignmentr.empty());
		consensus.reserve(alignmentr.size()-1 + alignmentf.size());
		consensus.assign(alignmentr.rbegin(), alignmentr.rend()-1);
		consensus.insert(consensus.end(),
				alignmentf.begin(), alignmentf.end());
	}
	return consensus;
}

/** Find an equivalent region of the two specified paths.
 * @return the consensus sequence
 */
static ContigPath align(
		const ContigPath& path1, const ContigPath& path2,
		const ContigNode& pivot)
{
	assert(find(path1.begin(), path1.end(), pivot) != path1.end());
	ContigPath::const_iterator it2 = find(path2.begin(), path2.end(),
			pivot);
	assert(it2 != path2.end());
	if (&path1 != &path2) {
		// The seed must be unique in path2, unless we're aligning a
		// path to itself.
		assert(count(it2+1, path2.end(), pivot) == 0);
	}

	ContigPath consensus;
	for (ContigPath::const_iterator it1 = find_if(
				path1.begin(), path1.end(),
				bind2nd(equal_to<ContigNode>(), pivot));
			it1 != path1.end();
			it1 = find_if(it1+1, path1.end(),
				bind2nd(equal_to<ContigNode>(), pivot))) {
		if (&*it1 == &*it2) {
			// We are aligning a path to itself, and this is the
			// trivial alignment, which we'll ignore.
			continue;
		}
		consensus = align(path1, path2, it1, it2);
		if (!consensus.empty())
			return consensus;
	}
	return consensus;
}
