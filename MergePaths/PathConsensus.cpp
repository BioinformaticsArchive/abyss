/**
 *
 * Resolve ambiguity ("N"s) in paths after MergePaths
 * by using pairwise/multiple sequence alignment
 * (pairwise: Needleman-Wunsch)
 * (multi: based on Dialign program by A.R.Subramanian)
 *
 * Author: Rong She (rshe@bcgsc.ca)
 *
 * Last Modified: July 2010
 */

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>

#include "dialign_para.h"
#include "dialign_struct.h"
#include "dialign_io.h"

#include "Common/smith_waterman.h"
#include "Common/needleman_wunsch.h"
#include "config.h"
#include "Common/Options.h"
#include "ContigNode.h"
#include "ContigPath.h"
#include "ContigGraph.h"
#include "Estimate.h"
#include "GraphAlgorithms.h"
#include "Dictionary.h"
#include "FastaReader.h"
#include "StringUtil.h"
#include <cerrno>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <iterator>
#include <set>
#include <map>
#include <string>
#include <vector>

using namespace std;

/* external functions for dialign */
extern struct alignment *create_empty_alignment(struct seq_col *scol,
	parameters *para);
extern struct diag_col *find_all_diags(struct scr_matrix *smatrix,
	struct prob_dist *pdist,
	struct seq_col *in_seq_col, struct alignment *algn,
	int round, parameters *para);
extern struct alignment *guided_aligner(struct alignment *palgn,
	struct seq_col *scol, struct diag_col *dcol,
	struct scr_matrix *smatrix, struct prob_dist *pdist,
	struct gt_node *gtn, int round, parameters *para);
extern char simple_aligner(struct seq_col *scol,
	struct diag_col *dcol,
	struct scr_matrix *smatrix, struct prob_dist *pdist,
	struct alignment *algn, int round, parameters *para);
extern void free_diag_col(struct diag_col *dcol);
extern void free_alignment(struct alignment *algn, parameters *para);

/* dialign */
void Dialign(vector<Sequence>& amb_seqs, Sequence& consensus);

/* ABySS options and messages */
#define PROGRAM "ResolveAmbPaths"

static const char VERSION_MESSAGE[] =
PROGRAM " (" PACKAGE_NAME ") " VERSION "\n"
"Written by Rong She.\n"
"\n"
"Copyright 2010 Canada's Michael Smith Genome Science Centre\n";

static const char USAGE_MESSAGE[] =
"Usage: " PROGRAM " [OPTION]... FASTA PATH ADJ\n"
"Resolve ambiguity (\"N\"s) in paths after MergePaths.\n"
"  FASTA    input fasta file that contains all contig sequences\n"
"  PATH     input \".path2\" file from MergePaths\n"
"  ADJ      input adj file (the entire adj graph)\n"
"\n"
"  -k, --kmer=KMER_SIZE  k-mer size\n"
"  -o, --out=FILE        write updated paths to FILE\n"
"  -f, --fa=FA_FILE      write updated contig sequences to FA_FILE\n"
"  -n, --align-num-paths=NUMPATHS  max number of paths to be\n"
"                                  aligned, default: 2\n"
"  -a, --align-identity=PID        min alignment identity\n"
"                                  default: 0.9\n"
"                                  (for pairwise alignment)\n"
"  -d, --dialign-debug=DEBUG       dialign debug level, default 0\n"
"                                  (for multi align)\n"
"  -s, --dialign-score=SCORE_MATRIX_FILE  score matrix file used\n"
"                                         by dialign\n"
"                                         (for multi align)\n"
"  -p, --dialign-prob=PROB_DIST_FILE      probability distribution\n"
"                                         file used by dialign\n"
"                                         (for multi align)\n"
"  -v, --verbose         display verbose output\n"
"      --help            display this help and exit\n"
"      --version         output version information and exit\n"
"\n"
"Report bugs to <" PACKAGE_BUGREPORT ">.\n";

namespace opt {
	unsigned k;
	static string out;
	static string fa;
	static double pid;
	static unsigned num_paths;
	static int dialign_debug;
	static string dialign_score;
	static string dialign_prob;
}

static const char shortopts[] = "k:o:f:a:n:d:s:p:v";

enum { OPT_HELP = 1, OPT_VERSION };

static const struct option longopts[] = {
	{ "kmer",        required_argument, NULL, 'k' },
	{ "out",         required_argument, NULL, 'o' },
	{ "fa",          required_argument, NULL, 'f' },
	{ "align-identity", required_argument, NULL, 'a' },
	{ "align-num-paths", required_argument, NULL, 'n' },
	{ "dialign-debug", required_argument, NULL, 'd' },
	{ "dialign-score", required_argument, NULL, 's' },
	{ "dialign-prob",  required_argument, NULL, 'p' },
	{ "verbose",     no_argument,       NULL, 'v' },
	{ "help",        no_argument,       NULL, OPT_HELP },
	{ "version",     no_argument,       NULL, OPT_VERSION },
	{ NULL, 0, NULL, 0 }
};

static struct {
	unsigned numPaths;
	unsigned numAmbPaths;
	unsigned numTooManySolutions;
	unsigned numNoSolutions;
	unsigned numMerged;
} stats;

/* some global data for dialign */
struct parameters *dialign_para;
map<string, char> IUPAC_codes;
struct scr_matrix *smatrix;
struct prob_dist *pdist;

/* ABySS global constructs */
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

struct AmbPathConstraint {
	ContigNode	source;
	ContigNode	dest;
	int		dist;

	AmbPathConstraint(ContigNode& src, ContigNode& dst, int d)
		: source(src), dest(dst), dist(d) {}

	bool operator ==(const AmbPathConstraint& a) const
	{
		return source == a.source && dest == a.dest
			&& dist == a.dist;
	}

	bool operator <(const AmbPathConstraint& a) const
	{
		return source < a.source
			|| (source == a.source && dest < a.dest)
			|| (source == a.source && dest == a.dest
				&& dist < a.dist);
	}
};

typedef ContigPath Path;
typedef vector<Path> ContigPaths;
typedef map<AmbPathConstraint, LinearNumKey> AmbPath2Contig;

/* global variables */
static vector<Contig> g_contigs;
LinearNumKey g_max_contigID;
AmbPath2Contig g_ambpath_contig;
map<LinearNumKey, ContigPaths> g_amb_paths;
static set< pair<ContigNode, ContigNode> > g_edges_irregular;

static double g_coverage_mean;
static double g_coverage_variance;

/* commonly-used utility function */
static void assert_open(ifstream& f, const string& p)
{
	if (f.is_open())
		return;
	cerr << p << ": " << strerror(errno) << endl;
	exit(EXIT_FAILURE);
}

/** Return the sequence of the specified contig node. The sequence
 * may be ambiguous or reverse complemented.
 */
const Sequence ContigNode::sequence() const
{
	if (ambiguous()) {
		string s(ambiguousSequence());
		if (s.length() < opt::k)
			transform(s.begin(), s.end(), s.begin(),
				::tolower);
		return string(opt::k - 1, 'N') + s;
	} else {
		const Sequence& seq = g_contigs[id()].seq;
		return sense() ? reverseComplement(seq) : seq;
	}
}

/** Return the coverage of this contig or zero if this contig is
 * ambiguous. */
unsigned ContigNode::coverage() const
{
	return ambiguous() ? 0 : g_contigs[id()].coverage;
}

/** Read contig paths from the specified file.
 * @param ids [out] the string ID of the paths
 */
static ContigPaths readPath2(const string& inPath,
	vector<string> *seeds, vector<bool> *isAmb)
{
	assert(seeds->empty()); //this seed is contigID of the path
	assert(isAmb->empty());
	assert(g_ambpath_contig.empty());
	ifstream fin(inPath.c_str());
	if (opt::verbose > 0)
		cerr << "Reading `" << inPath << "'..." << endl;
	if (inPath != "-")
		assert_open(fin, inPath);
	istream& in = inPath == "-" ? cin : fin;

	ContigPaths paths;
	string id;
	Path path;
	Path::iterator prev, next;
	while (in >> id >> path) {
		paths.push_back(path);
		seeds->push_back(id);
		unsigned path_id = strtoul(id.c_str(), NULL, 0);

		/* update g_max_contigID */
		if (path_id > g_max_contigID)
			g_max_contigID = path_id;

		if (path.size() <= 2) {
			isAmb->push_back(false);
			continue;
		}

		/* contig with Ns must have prev and next */
		bool cur_is_amb = false;
		prev = path.begin();
		next = path.begin();
		Path::iterator it = path.begin();
		it++; next++; next++;
		for (; next != path.end(); it++, prev++, next++) {
			if (it->ambiguous()) {
				cur_is_amb = true;
				/* add the entry to g_ambpath_contig
				(value 0: unprocessed/no proper paths) */
				g_ambpath_contig.insert(AmbPath2Contig::value_type(
					AmbPathConstraint(*prev, *next, -it->id()), 0));
			}
		}
		isAmb->push_back(cur_is_amb);
	}
	assert(in.eof());
	return paths;
}


/** Read ambiguous path info from SimpleGraph verbose output.
 * format:
 * 275+ 1181+ 1287- 614+
 * 275+ 1198+ 1287- 614+
 * 275+ 73N 1287- 614+
 *
 * Store path info in "g_amb_paths"
 */ /*
static void readAmbPaths(const string& ambPath)
{
	ifstream fin(ambPath.c_str());
	if (opt::verbose > 0)
		cerr << "Reading `" << ambPath << "'..." << endl;
	if (ambPath != "-")
		assert_open(fin, ambPath);
	istream& in = ambPath == "-" ? cin : fin;

	vector<Path> paths;
	string id;
	Path path;
	bool isAmbPath;
	while (in >> path) {
		isAmbPath = false;
		for (Path::iterator it = path.begin();
			it != path.end(); it++)
			if (it->ambiguous()) {
				isAmbPath = true;
				assert(!paths.empty());
				g_amb_paths.insert(
					map<Path, vector<Path> >::value_type(
					path, paths));
				paths.clear();
				break;
			}
		if (!isAmbPath)
			paths.push_back(path);
	}
	assert(in.eof());
}
*/

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

/* Reset the seen-mark on contigs that are in unambiguous paths */
static void unseenContigs(vector<bool>& seen,
	const vector<Path>& paths)
{
	for (vector<Path>::const_iterator it = paths.begin();
			it != paths.end(); it++)
		for (Path::const_iterator itc = it->begin();
				itc != it->end(); itc++)
			if (itc->id() < seen.size()
					&& seen[itc->id()])
				seen[itc->id()] = false;
}

/* read in adj file, store which contigs have irregular overlaps
 * (>=k-1 or non-exact match)
 */ /*
void ReadAdj(const string& irregularAdj)
{
	ifstream in_adj(irregularAdj.c_str());
	assert_open(in_adj, irregularAdj);
	while (!in_adj.eof()) {
		char line[65536];
		in_adj.getline(line, 65536);
		string s = line;
		vector<string> strs;
		size_t start_pos = 0, delimit_pos;
		while (start_pos < s.length()
			&& (delimit_pos = s.find(';', start_pos))
			!= string::npos) {
			//get first, second parts
			if (delimit_pos > start_pos + 1)
				strs.push_back(s.substr(start_pos,
					delimit_pos-start_pos));
			else
				strs.push_back("");
			start_pos = delimit_pos+1;
		}
		//ignore lines that do not have two ';'s
		if (strs.size() < 2)
			continue;
		//get the last part
		if (start_pos + 1 < s.length())
			strs.push_back(s.substr(start_pos));
		else
			strs.push_back("");
		assert(strs.size() == 3);
		size_t space_pos;
		assert ((space_pos = strs[0].find(' '))
			!= string::npos);
		string t_id = strs[0].substr(0,space_pos);
		if (!strs[1].empty()) {
			start_pos = 0;
			while (start_pos < strs[1].length()
				&& (delimit_pos = strs[1].find_first_of(" \t", start_pos))
				!= string::npos) {
				if (delimit_pos > start_pos) {
					string h_id =
						strs[1].substr(start_pos, delimit_pos-start_pos-1);
					bool h_sense =
						(strs[1][delimit_pos-1] == '+'? false : true);

					g_edges_irregular.insert(pair<ContigNode, ContigNode>(
						ContigNode(t_id, false),
						ContigNode(h_id, h_sense) ));

					g_edges_irregular.insert(pair<ContigNode, ContigNode>(
						ContigNode(h_id, !h_sense),
						ContigNode(t_id, true) ));
				}
				start_pos = delimit_pos + 1;
			}
		}
		if (!strs[2].empty()) {
			start_pos = 0;
			while (start_pos < strs[2].length()
				&& (delimit_pos = strs[2].find_first_of(" ", start_pos))
				!= string::npos) {
				if (delimit_pos > start_pos) {
					string h_id =
						strs[2].substr(start_pos, delimit_pos-start_pos-1);
					bool h_sense =
						(strs[2][delimit_pos-1] == '+'? false : true);

					g_edges_irregular.insert(pair<ContigNode, ContigNode>(
						ContigNode(h_id, h_sense),
						ContigNode(t_id, false) ));

					g_edges_irregular.insert(pair<ContigNode, ContigNode>(
						ContigNode(t_id, true),
						ContigNode(h_id, !h_sense) ));

				}
				start_pos = delimit_pos + 1;
			}
		}
	}
	in_adj.close();
	if (opt::verbose > 0) {
		cerr << "g_edges_irregular:\n";
		set< pair<ContigNode, ContigNode> >::const_iterator it =
			g_edges_irregular.begin();
		for (; it != g_edges_irregular.end(); it++)
			cerr << it->first << " to "
				<< it->second << endl;
	}
}
*/

static LinearNumKey createNewContig(std::string& contigKey,
	const Sequence& seq, const unsigned coverage)
{
	ostringstream oss;
	oss << ++g_max_contigID;
	contigKey = oss.str();
	LinearNumKey id = g_contigIDs.serial(contigKey);
	g_contigs.push_back(Contig(contigKey, seq, coverage));
	return id;
}


static LinearNumKey outputNewContig(ofstream& fa,
	const vector<Path>& solutions,
	size_t longestPrefix, size_t longestSuffix,
	const Sequence& seq, const unsigned coverage)
{
	/* update g_contigs and g_contigIDs, get id/contigKey */
	std::string contigKey;
	LinearNumKey id = createNewContig(contigKey, seq, coverage);

	fa << '>' << contigKey << ' ' << seq.length()
		<< " " << coverage << " ";
	for (vector<Path>::const_iterator it = solutions.begin();
			it != solutions.end(); it++) {
		size_t i=longestPrefix-1;
		for (; i<(*it).size()-longestSuffix; i++)
			fa << (*it)[i] << ',';
		fa << (*it)[i] << ";";
	}
	fa << '\n' << seq << '\n';
	if (opt::verbose > 0) {
		cerr << "g_contigIDs.size():"
			<< g_contigIDs.map_size()
			<< ";" << g_contigIDs.keys()
			<< "; g_contigs.size():" << g_contigs.size()
			<< "; g_contigIDs.key(" << id << "):"
			<< contigKey << "; seq:" << seq
			<< "; coverage:" << coverage << endl;
	}
	return id;
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

/** Merge the specified two contigs,
 * With modified Overlap, we no longer assume overlap is k-1, instead,
 * do a smith-waterman alignment between contigs to determine their
 * overlap.
*/
static overlap_align mergeContigs_SW(Sequence& seq, const Sequence& s,
	const ContigNode& node, const Path& path)
{
	vector<overlap_align> overlaps;
	overlaps.reserve(1); /* only need the longest overlap */
	do {
		unsigned len = min(seq.length(), s.length());
		assert(len > opt::k - 1);
		GetLocalAlignment(seq.substr(seq.length()-len),
			s.substr(0, len),
			seq.length()-len, overlaps, false,
			opt::verbose > 0);
	} while (overlaps.empty() && chomp(seq, 'n'));

	if (overlaps.empty()) {
		cerr << "warning: the head of `" << node << "' "
			"does not match the tail of the previous contig\n"
			<< seq << '\n' << s << '\n' << path << endl;
		seq += 'n';
		seq += s;
		return overlap_align();
	} else {
		seq.erase(overlaps[0].overlap_t_pos);
		seq += overlaps[0].overlap_str;
		seq += Sequence(s, overlaps[0].overlap_h_pos+1);
		return overlaps[0];
	}
}

/** Merge the specified two contigs, default overlap is k-1,
 * generate a consensus sequence of the overlapping region. The result
 * is stored in the first argument.
 */
static void mergeContigs(Sequence& seq, const Sequence& s,
	const ContigNode& node, const Path& path)
{
	unsigned overlap = opt::k - 1;
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
		cerr << "warning: the head of `" << node << "' "
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

static Contig mergePath(const Path& path)
{
	Sequence seq;
	unsigned coverage = 0;
	Path::const_iterator prev_it;
	for (Path::const_iterator it = path.begin();
			it != path.end(); ++it) {
		coverage += it->coverage();
		if (seq.empty()) {
			seq = it->sequence();
		} else {
			if (g_edges_irregular.find(
					pair<ContigNode, ContigNode>(
					*prev_it, *it))
					== g_edges_irregular.end())
				mergeContigs(seq, it->sequence(),
					*it, path);
			else
				mergeContigs_SW(seq, it->sequence(),
					*it, path);
		}
		prev_it = it;
	}
	return Contig("", seq, coverage);
}

static Sequence pathSequence_no_overlap(Contig& pathContig)
{
	Sequence& pathseq = pathContig.seq;
	pathseq.erase(pathseq.length()-opt::k+1);
	pathseq.erase(0, opt::k-1);
	return pathseq;
}

/* validate path coverage, at 95% confidence interval */
bool ValidCoverage(unsigned pathLen, unsigned pathCover)
{
	double cover_mean
		= (double)pathCover / (pathLen + opt::k - 1);
	double cover_deviation
		= sqrt(g_coverage_variance / (pathLen + opt::k - 1));
	return cover_mean <= g_coverage_mean + 1.96*cover_deviation
		&& cover_mean >= g_coverage_mean - 1.96*cover_deviation;
}

/* Resolve ambiguous region using pairwise alignment (needleman-wunsch)
 * ('solutions' contain exactly two paths, from a source contig
 * to a dest contig)
 */
static LinearNumKey ResolvePairAmbPath(const ContigPaths& solutions,
	ofstream& fa)
{
	assert(solutions.size() == 2
		&& solutions[0].front() == solutions[1].front()
		&& solutions[0].back() == solutions[1].back());
	ContigPath fstSol = solutions[0];
	ContigPath sndSol = solutions[1];
	assert(fstSol.size()>2 && sndSol.size()>2);
	fstSol.pop_back();
	fstSol.erase(fstSol.begin());
	sndSol.pop_back();
	sndSol.erase(sndSol.begin());

	Contig fstPathContig = mergePath(fstSol);
	Sequence fstseq = pathSequence_no_overlap(fstPathContig);

	Contig sndPathContig = mergePath(sndSol);
	Sequence sndseq = pathSequence_no_overlap(sndPathContig);

	NWAlignment align;
	unsigned match = GetGlobalAlignment(fstseq, sndseq, align,
		opt::verbose>0);
	unsigned coverage = fstPathContig.coverage
		+ sndPathContig.coverage;
	if ((double)match / align.size() < opt::pid
		|| !ValidCoverage(align.size(), coverage))
		return 0;

	// add k-1 extensions at both ends of consensus sequence
	Sequence consensus = align.consensus();
	const Sequence& prev_seq = solutions[0].front().sequence();
	consensus.insert(0,
		prev_seq.substr(prev_seq.length()-opt::k+1));
	const Sequence& next_seq = solutions[0].back().sequence();
	consensus += next_seq.substr(0, opt::k-1);

	LinearNumKey new_contig_id = outputNewContig(fa, solutions,
		1, 1, consensus, coverage);
	return new_contig_id;
}

/* Resolve ambiguous region using multiple alignment of all paths in
 * `solutions'.
 */
static LinearNumKey ResolveAmbPath(const vector<Path>& solutions,
	ofstream& fa)
{
	if (opt::verbose > 0) {
		cerr << "input paths:" << endl;
		for (vector<Path>::const_iterator solIter =
				solutions.begin();
				solIter != solutions.end(); solIter++)
			cerr << *solIter << endl;
	}

	// Find the size of the smallest path.
	const Path& firstSol = solutions.front();
	size_t min_len = firstSol.size();
	for (vector<Path>::const_iterator it = solutions.begin() + 1;
			it != solutions.end(); ++it)
		min_len = min(min_len, it->size());

	// Find the longest prefix.
	Path vppath;
	size_t longestPrefix;
	bool commonPrefix = true;
	for (longestPrefix = 0; longestPrefix < min_len; longestPrefix++) {
		const ContigNode& common_path_node = firstSol[longestPrefix];
		for (vector<Path>::const_iterator solIter = solutions.begin();
				solIter != solutions.end(); ++solIter) {
			const ContigNode& pathnode = (*solIter)[longestPrefix];
			if (pathnode != common_path_node) {
				// Found the longest prefix.
				commonPrefix = false;
				break;
			}
		}
		if (!commonPrefix)
			break;
		vppath.push_back(common_path_node);
	}

	// Find the longest suffix.
	Path vspath;
	size_t longestSuffix;
	bool commonSuffix = true;
	for (longestSuffix = 0;	longestSuffix < min_len-longestPrefix;
			longestSuffix++) {
		const ContigNode& common_path_node
			= firstSol[firstSol.size()-longestSuffix-1];
		for (vector<Path>::const_iterator solIter = solutions.begin();
				solIter != solutions.end(); ++solIter) {
			const ContigNode& pathnode
				= (*solIter)[solIter->size()-longestSuffix-1];
			if (pathnode != common_path_node) {
				// Found the longest suffix.
				commonSuffix = false;
				break;
			}
		}
		if (!commonSuffix)
			break;
		vspath.push_back(common_path_node);
	}

	if (opt::verbose > 0)
		cerr << "prefix path:" << vppath << endl
			<< "suffix path:" << vspath << endl;

	// Get sequence of ambiguous region in paths
	assert(longestPrefix > 0 && longestSuffix > 0);
	vector<Sequence> amb_seqs;
	unsigned coverage = 0;
	for (vector<Path>::const_iterator solIter = solutions.begin();
			solIter != solutions.end(); solIter++) {
		Path cur_path;
		for (size_t index = longestPrefix;
				index < (*solIter).size() - longestSuffix;
				index++)
			cur_path.push_back( (*solIter)[index] );
		//skip if the ambiguous region is empty
		if (cur_path.empty())
			continue;
		Contig newContig = mergePath(cur_path);
		Sequence& newseq = newContig.seq;
		coverage += newContig.coverage;
		//find overlap between newContig and Suffix,
		//remove it from newContig sequence
		if (g_edges_irregular.find(pair<ContigNode, ContigNode>(
				(*solIter)[(*solIter).size()-longestSuffix-1],
				(*solIter)[(*solIter).size()-longestSuffix]))
				== g_edges_irregular.end()) {
			newseq.erase(newseq.length()-opt::k+1);
		} else {
			Sequence lastSeq
				= (*solIter)[(*solIter).size()-longestSuffix-1].sequence();
			Sequence suffixSeq
				= (*solIter)[(*solIter).size()-longestSuffix].sequence();
			overlap_align ol = mergeContigs_SW(lastSeq,
				suffixSeq,
				(*solIter)[(*solIter).size()-longestSuffix],
				*solIter);
			newseq.erase(ol.overlap_t_pos);
		}
		//find overlap between Prefix and newContig,
		//remove it from newContig sequence
		if (g_edges_irregular.find(pair<ContigNode, ContigNode>(
				(*solIter)[longestPrefix-1], (*solIter)[longestPrefix]))
				== g_edges_irregular.end()) {
			newseq.erase(0, opt::k-1);
		} else {
			Sequence prefixSeq
				= (*solIter)[longestPrefix-1].sequence();
			Sequence firstSeq
				= (*solIter)[longestPrefix].sequence();
			overlap_align ol = mergeContigs_SW(prefixSeq,
				firstSeq, (*solIter)[longestPrefix], *solIter);
			newseq.erase(0, ol.overlap_h_pos+1);
		}
		amb_seqs.push_back(newseq);
	}

	// Do multiple sequence alignment
	Sequence consensus;
	Dialign(amb_seqs, consensus);

	// check coverage
	if (!ValidCoverage(consensus.length(), coverage))
		return 0;

	// add k-1 extensions at both ends
	const Path& fstPath = solutions.front();
	const Sequence& prev_seq
		= fstPath[longestPrefix-1].sequence();
	consensus.insert(0, prev_seq.substr(prev_seq.length()-opt::k+1));
	const Sequence& next_seq
		= fstPath[fstPath.size()-longestSuffix].sequence();
	consensus += next_seq.substr(0, opt::k-1);
	// output consensus as new contig
	if (opt::verbose > 0)
		cerr << "prefix path:" << vppath << endl
			<< "suffix path:" << vspath << endl
			<< "consensus:" << consensus << endl;
	LinearNumKey new_contig_id = outputNewContig(fa, solutions,
		longestPrefix, longestSuffix, consensus, coverage);

	/*ContigNode new_contig_node(new_contig_id, false);
	if (opt::verbose > 0)
		cerr << "new node:" << new_contig_node << endl;
	vppath.push_back(new_contig_node);
	for (int i=vspath.size()-1; i>=0; i--)
		vppath.push_back(vspath[i]);

	if (opt::verbose > 0) {
		cerr << "input paths:" << endl;
		for (vector<Path>::const_iterator solIter = solutions.begin();
			solIter != solutions.end(); solIter++)
			cerr << *solIter << endl;
		cerr << "output path:" << vppath << endl;
	}
	return vppath;*/

	return new_contig_id;
}

void InitIUPAC()
{
	IUPAC_codes.insert(map<string, char>::value_type(string("AG"), 'R'));
	IUPAC_codes.insert(map<string, char>::value_type(string("CT"), 'Y'));
	IUPAC_codes.insert(map<string, char>::value_type(string("AC"), 'M'));
	IUPAC_codes.insert(map<string, char>::value_type(string("GT"), 'K'));
	IUPAC_codes.insert(map<string, char>::value_type(string("CG"), 'S'));
	IUPAC_codes.insert(map<string, char>::value_type(string("AT"), 'W'));
	IUPAC_codes.insert(map<string, char>::value_type(string("ACT"), 'H'));
	IUPAC_codes.insert(map<string, char>::value_type(string("CGT"), 'B'));
	IUPAC_codes.insert(map<string, char>::value_type(string("ACG"), 'V'));
	IUPAC_codes.insert(map<string, char>::value_type(string("AGT"), 'D'));

	//also reverse order for pairwise consensus
	IUPAC_codes.insert(map<string, char>::value_type(string("GA"), 'R'));
	IUPAC_codes.insert(map<string, char>::value_type(string("TC"), 'Y'));
	IUPAC_codes.insert(map<string, char>::value_type(string("CA"), 'M'));
	IUPAC_codes.insert(map<string, char>::value_type(string("TG"), 'K'));
	IUPAC_codes.insert(map<string, char>::value_type(string("GC"), 'S'));
	IUPAC_codes.insert(map<string, char>::value_type(string("TA"), 'W'));
}

void LoadProbDist()
{
	// read similarity matrix
	smatrix = read_scr_matrix(dialign_para->SCR_MATRIX_FILE_NAME,
		dialign_para);

	// print the score matrix
	if( dialign_para->DEBUG >5)
		print_scr_matrix(smatrix);

	// read the probability distribution for diagonals
	pdist = read_diag_prob_dist(smatrix,
		dialign_para->DIAG_PROB_FILE_NAME, dialign_para);
}

void CompCoverageStatistics()
{
	int num = g_contigs.size();
	double coverage = 0, variance = 0;
	for (vector<Contig>::const_iterator it = g_contigs.begin();
			it != g_contigs.end(); it++) {
		double c = (double)(it->coverage)
			/ (it->seq.length() - opt::k + 1);
		coverage += c;
		variance += c * c;
	}
	g_coverage_mean = coverage / num;
	g_coverage_variance = (variance - 2 * g_coverage_mean * coverage)
		/ num + g_coverage_mean * g_coverage_mean;
}

/**
 * main program routine
 */
int main(int argc, char **argv)
{
	ContigPath::separator = ",";
	opt::dialign_debug = 0;
	opt::pid = 0.9;
	opt::num_paths = 2;

	bool die = false;
	for (int c; (c = getopt_long(argc, argv,
			shortopts, longopts, NULL)) != -1;) {
		istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
		case '?': die = true; break;
		case 'k': arg >> opt::k; break;
		case 'o': arg >> opt::out; break;
		case 'a': arg >> opt::pid; break;
		case 'n': arg >> opt::num_paths; break;
		case 'f': arg >> opt::fa; break;
		case 'd': arg >> opt::dialign_debug; break;
		case 's': arg >> opt::dialign_score; break;
		case 'p': arg >> opt::dialign_prob; break;
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

	if (opt::fa.empty()) {
		cerr << PROGRAM ": " << "missing -f,--fa option\n";
		die = true;
	}

	if (opt::dialign_score.empty()) {
		cerr << PROGRAM ": " << "missing -s,--dialign-score option\n";
		die = true;
	}

	if (opt::dialign_prob.empty()) {
		cerr << PROGRAM ": " << "missing -p,--dialign-prob option\n";
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

	const char *contigFile = argv[optind++];
	string allPaths(argv[optind++]);
	string adjFile(argv[optind++]);

	// Load the graph from the adjacency file
	ContigGraph contigGraph;
	loadGraphFromAdjFile(&contigGraph, adjFile);
	if (opt::verbose > 0)
		cout << "Vertices: " << contigGraph.num_vertices()
			<< " Edges: " << contigGraph.num_edges() << endl;

	// Read contigs
	vector<Contig>& contigs = g_contigs;
	{
		FastaReader in(contigFile,
			FastaReader::KEEP_N | FastaReader::NO_FOLD_CASE);
		for (FastaRecord rec; in >> rec;) {
			istringstream ss(rec.comment);
			unsigned length, coverage = 0;
			ss >> length >> coverage;
			unsigned serial = g_contigIDs.serial(rec.id);
			assert(contigs.size() == serial);
			(void)serial;
			contigs.push_back(Contig(rec.id, rec.seq, coverage));
		}
		assert(in.eof());
		assert(!contigs.empty());
		opt::colourSpace = isdigit(contigs[0].seq[0]);
		//if (argc - optind == 0) g_contigIDs.lock();
	}
	g_max_contigID = g_contigs.size()-1;
	g_contigIDs.unlock();

	// Get contig k-mer-coverage statistics
	CompCoverageStatistics();

	vector<string> pathIDs;
	vector<bool> isAmbPath;
	ContigPaths paths = readPath2(allPaths, &pathIDs, &isAmbPath);
	stats.numPaths = paths.size();
	stats.numAmbPaths = g_ambpath_contig.size();
	if (opt::verbose > 0) {
		cerr << "Total number of paths: " << stats.numPaths << endl;
		cerr << "ambiguous paths: " << stats.numAmbPaths << endl;
	}

	// Prepare output fasta file
	ofstream fa(opt::fa.c_str());
	assert(fa.good());

	dialign_para = new parameters();
	dialign_para->DEBUG = opt::dialign_debug;
	dialign_para->SCR_MATRIX_FILE_NAME
		= (char*)opt::dialign_score.c_str();
	dialign_para->DIAG_PROB_FILE_NAME
		= (char*)opt::dialign_prob.c_str();
	InitIUPAC();
	LoadProbDist();

	// resolve ambiguous paths recorded in g_ambpath_contig
	for (AmbPath2Contig::iterator ambIt = g_ambpath_contig.begin();
			ambIt != g_ambpath_contig.end(); ambIt++) {
		AmbPathConstraint apConstraint = ambIt->first;
		if (opt::verbose > 0) {
			cerr << "constraint: source:" << apConstraint.source
				<< ";dest:" << apConstraint.dest
				<< ";dist:" << apConstraint.dist << endl;
		}
		ContigPaths solutions;
		unsigned numVisited = 0;
		Constraints constraints; //only 1 constraint here
		constraints.push_back(Constraint(apConstraint.dest,
			apConstraint.dist + opt::k - 1 + allowedError(0)));
		if (opt::verbose > 0) {
			cerr << "constraint:";
			for (Constraints::const_iterator it
					= constraints.begin();
					it != constraints.end(); ++it)
				cerr << ' ' << it->first << ',' << it->second;
			cerr << endl;
		}
		depthFirstSearch(contigGraph, apConstraint.source,
			constraints, solutions, numVisited);
		if (opt::verbose > 0)
			cerr << "solutions:" << endl;
		for (ContigPaths::iterator solIt = solutions.begin();
				solIt != solutions.end(); solIt++) {
			solIt->insert(solIt->begin(), apConstraint.source);
			if (opt::verbose > 0)
				cerr << *solIt << endl;
		}
		unsigned numPossiblePaths = solutions.size();
		bool tooManySolutions = numPossiblePaths > opt::num_paths;
		LinearNumKey new_contig_id = 0;
		if (tooManySolutions) {
			stats.numTooManySolutions++;
			cerr << "too many solutions: "
				<< numPossiblePaths << endl;
		} else if (numPossiblePaths == 2) { //2 solutions
			//assert(numPossiblePaths == 2);
			new_contig_id = ResolvePairAmbPath(solutions, fa);
		} else if (numPossiblePaths > 2) {//3 paths or more
			new_contig_id = ResolveAmbPath(solutions, fa);
		} else if (numPossiblePaths == 1) { //1 path, use it
			//create a fake contigID to be replaced later
			std::string contigKey;
			Sequence seq = "";
			new_contig_id = createNewContig(contigKey, seq, 0);
		} else { //no path
			/* TODO: call shortest-path algorithm
			to check whether there IS path */
			stats.numNoSolutions++;
			cerr << "ERROR? found 0 path" << endl;
		}
		if (new_contig_id > 0) {
			stats.numMerged++;
			//update ambIt->second to the new contig ID
			ambIt->second = new_contig_id;
			//collect the used paths in g_amb_paths
			g_amb_paths.insert(
				map<LinearNumKey, ContigPaths>::value_type(
					new_contig_id, solutions));
		}
	}

	// Record all the contigs that are in a path.
	vector<bool> seen(contigs.size());
	//seenContigs(seen, paths);

	//collect contigs on paths used in ambiguous path resolving
	for (map<LinearNumKey, ContigPaths>::iterator mapIt
			= g_amb_paths.begin(); mapIt != g_amb_paths.end();
			mapIt++)
		if ((mapIt->second).size() > 1)
			seenContigs(seen, mapIt->second);

	//unmark contigs that are used in unambiguous paths
	unseenContigs(seen, paths);

	// Output those contigs that were not seen in ambiguous path.
	for (vector<Contig>::const_iterator it = contigs.begin();
			it != contigs.end(); ++it)
		if (!seen[it - contigs.begin()])
			fa << *it;

	// read in adj file
	//ReadAdj(irregularAdj);

	ofstream out(opt::out.c_str());
	assert(out.good());

	for (unsigned i=0; i<paths.size(); i++) {
		if (!isAmbPath[i]) {
			out << pathIDs[i] << "\t" << paths[i] << endl;
			continue;
		}

		//ambiguous path, must have at least 3 nodes
		Path::iterator prev = paths[i].begin();
		Path::iterator next = paths[i].begin();
		Path::iterator it = paths[i].begin();
		Path cur_path; //used to collect updated path
		cur_path.push_back(*it);
		it++;
		next++;
		next++;
		for (; it != paths[i].end(); it++, prev++, next++) {
			if (!it->ambiguous()) {
				cur_path.push_back(*it);
				continue;
			}

			//replace Ns by resolved new contig
			assert(next != paths[i].end());
			AmbPath2Contig::iterator ambIt = g_ambpath_contig.find(
				AmbPathConstraint(*prev, *next, -it->id()));
			assert(ambIt != g_ambpath_contig.end());
			LinearNumKey cid = ambIt->second;
			if (cid > 0) {
				ContigPaths& solutions
					= (g_amb_paths.find(cid))->second;
				if (solutions.size() > 1) {
					cur_path.push_back(
						ContigNode(cid, false));
				} else {
					ContigPath& sol = solutions[0];
					assert(sol.size() > 1);
					for (unsigned j = 1; j < sol.size()-1;
							j++)
						cur_path.push_back(sol[j]);
				}
			} else {
				cur_path.push_back(*it);
			}
		}
		//use same pathID as before
		out << pathIDs[i] << "\t" << cur_path << endl;
	}

	free_prob_dist(pdist);
	delete dialign_para;

	out.close();
	fa.close();

	cerr << "Total ambiguous paths attempted: " << stats.numAmbPaths
		<< "\n No valid solutions: " << stats.numNoSolutions
		<< "\n Too many solutions: " << stats.numTooManySolutions
		<< "\n Failed PID or coverage: "
		<< stats.numAmbPaths - stats.numMerged
			- stats.numNoSolutions - stats.numTooManySolutions
		<< "\n Merged: " << stats.numMerged << "\n";
}

/* DIALIGN-TX v1.0.2: multiple sequence alignment algorithm */
void Dialign(vector<Sequence>& amb_seqs, Sequence& consensus)
{
	int i;
	struct seq_col *in_seq_col=NULL;
	double tim = clock();

	in_seq_col = read_seqs(amb_seqs, dialign_para);

	// fast mode has higher threshold weights
	if(dialign_para->FAST_MODE)
		dialign_para->PROT_SIM_SCORE_THRESHOLD += 0.25;

	// Consider Anchors -> default for DNA: DO_ANCHOR = 0;
	struct alignment *algn= NULL;
	if (!dialign_para->FAST_MODE)
		algn = create_empty_alignment(in_seq_col, dialign_para);
	struct alignment *salgn = create_empty_alignment(in_seq_col,
		dialign_para);
	if (dialign_para->DEBUG >1)
		printf("empty alignments created\n");

	// Compute pairwise diagonals
	struct diag_col *all_diags = find_all_diags(smatrix, pdist,
		in_seq_col,salgn,1, dialign_para);
	double duration = (clock()-tim)/CLOCKS_PER_SEC;
	if (dialign_para->DEBUG >1)
		printf("Found %i diags in %f secs\n",
			all_diags->diag_amount, duration);
	int diag_amount = all_diags->diag_amount;

	// Compute alignment
	double tim2=clock();
	if (!dialign_para->FAST_MODE) {
		struct diag *cp_diags[all_diags->diag_amount];
		for(i=0;i<diag_amount;i++) {
			cp_diags[i] = (diag*)malloc(sizeof(struct diag));
			*(cp_diags[i]) = *(all_diags->diags[i]);
		}
		guided_aligner(algn, in_seq_col, all_diags, smatrix,
			pdist, all_diags->gt_root, 1, dialign_para);

		for(i=0;i<diag_amount;i++)
			all_diags->diags[i] = cp_diags[i];

		all_diags->diag_amount = diag_amount;
	}
	simple_aligner(in_seq_col, all_diags, smatrix, pdist,
		salgn, 1, dialign_para);
	duration = (clock()-tim2)/CLOCKS_PER_SEC;

	if (!dialign_para->FAST_MODE) {
		if (dialign_para->DEBUG >1)
			printf("First alignment after %f secs. simple: %f guided: %f\n",
				duration, salgn->total_weight, algn->total_weight);
		else
			if (dialign_para->DEBUG > 1)
				printf("First alignment after %f secs. simple: %f \n",
					duration, salgn->total_weight);
	}

	free_diag_col(all_diags);

	dialign_para->DO_ANCHOR = 0; // anchors done

	// round 2+
	int round;
	char newFound = 0;
	int type;

	// consider sensitivity level
	if (!dialign_para->FAST_MODE) {
		if (dialign_para->SENS_MODE==0) {
			dialign_para->DIAG_THRESHOLD_WEIGHT = 0.0;
		} else if (dialign_para->SENS_MODE==1) {
			dialign_para->DIAG_THRESHOLD_WEIGHT
				= -log(0.75);//-log(.875+0.125/2.0);
		} else if (dialign_para->SENS_MODE==2) {
			dialign_para->DIAG_THRESHOLD_WEIGHT
				= -log(0.5);//-log(0.875);
		}
	}

	int stype = (dialign_para->FAST_MODE ? 1 : 0);
	for (type=stype; type<2; type++) {
		for (round=2; round<=20; round++) {
			tim2=clock();
			all_diags = find_all_diags(smatrix, pdist,
				in_seq_col, (type ? salgn : algn),
				round, dialign_para);
			duration = (clock()-tim2)/CLOCKS_PER_SEC;
			if (dialign_para->DEBUG >1)
				printf("Found %i diags after %f secs\n",
					all_diags->diag_amount, duration);
			if (all_diags->diag_amount ==0) {
				free_diag_col(all_diags);
				break;
			} else {
			// round 2 and further we use the simple aligner
				newFound = simple_aligner(in_seq_col,
					all_diags, smatrix, pdist,
					(type ? salgn : algn), round,
					dialign_para);
				free_diag_col(all_diags);
				if (!newFound)
					break;
			}
		}
	}
	if (dialign_para->DEBUG >1)
		printf("Alignment ready!\n");

	if (!dialign_para->FAST_MODE) {
		if (dialign_para->DEBUG >1)
			printf("Final alignment simple: %f guided: %f\n",
				salgn->total_weight, algn->total_weight);
	} else {
		if (dialign_para->DEBUG >1)
			printf("Final alignment simple: %f \n",
				salgn->total_weight);
	}

	if (dialign_para->FAST_MODE
			|| (salgn->total_weight > algn->total_weight)) {
		if (!dialign_para->FAST_MODE)
			free_alignment(algn, dialign_para);
		algn = salgn;
	} else {
		free_alignment(salgn, dialign_para);
	}

	get_alignment_consensus(algn, consensus, dialign_para);

	duration = (clock()-tim)/CLOCKS_PER_SEC;
	printf("Total time:   %f secs\n", duration);
	printf("Total weight: %f \n", algn->total_weight);

	free_alignment(algn, dialign_para);
	free_seq_col(in_seq_col);
}
