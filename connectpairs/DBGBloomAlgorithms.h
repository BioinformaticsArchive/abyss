#ifndef DBGBLOOMALGORITHMS_H
#define DBGBLOOMALGORITHMS_H 1

#include "Common/Kmer.h"
#include "DBGBloom.h"
#include "Common/StringUtil.h"
#include "DataLayer/FastaReader.h"
#include "Graph/DefaultColorMap.h"
#include "Graph/Path.h"
#include "Graph/BreadthFirstSearch.h"
#include "Graph/ConstrainedBFSVisitor.h"

using namespace std;

#define SUPPRESS_UNUSED_WARNING(a) (void)a

typedef ConstrainedBFSVisitor<DBGBloom>::Path Path;
typedef ConstrainedBFSVisitor<DBGBloom>::PathList PathList;

inline static Sequence pathToSeq(Path path)
{
	Sequence seq;
	assert(path.size() > 0);
	seq.append(path[0].str());
	for (unsigned i = 1; i < path.size(); i++) {
		string kmer = path[i].str();
		seq.append(kmer.substr(kmer.length() - 1, 1));
	}
	return seq;
}

static PathSearchResult connectPairs(
	FastaRecord& read1, 
	FastaRecord& read2, 
	const DBGBloom& g,
	vector<FastaRecord>& mergedSeqs,
	int maxPaths = 2,
	int maxMergedSeqLen = NO_LIMIT)
{
	SUPPRESS_UNUSED_WARNING(connectPairs);

	unsigned k = g.m_k;

	assert(isReadNamePair(read1.id, read2.id));

	if (read1.seq.length() < k || read2.seq.length() < k)
		return NO_PATH;

	string kmer1Str = read1.seq.substr(0, k);
	string kmer2Str = read2.seq.substr(0, k); 

	// TODO: advance to next kmers in the reads instead of giving up
	if (kmer1Str.find_first_not_of("AGCTagct") != string::npos ||
		kmer2Str.find_first_not_of("AGCTagct") != string::npos)
		return NO_PATH;
	
	// TODO: add option for mate pair orientation (RF)
	Kmer kmer1(kmer1Str);
	Kmer kmer2(kmer2Str);
	kmer2.reverseComplement();

	// TODO: advance to next kmers in the reads instead of giving up
	if (!graph_traits<DBGBloom>::vertex_exists(kmer1, g) || !graph_traits<DBGBloom>::vertex_exists(kmer2, g)) 
		return NO_PATH;

	int maxPathLen;
	if (maxMergedSeqLen== NO_LIMIT) {
		maxPathLen = NO_LIMIT;
	} else {
		assert(maxMergedSeqLen > 0);
		maxPathLen = maxMergedSeqLen - k + 1;
	}

	DefaultColorMap<DBGBloom> colorMap;
	ConstrainedBFSVisitor<DBGBloom> visitor(kmer1, kmer2, 0, maxPathLen, colorMap);
	breadthFirstSearch(g, kmer1, visitor, colorMap);

	PathList pathsFound;
	PathSearchResult result = visitor.pathsToGoal(pathsFound, maxPaths);

	if (result == FOUND_PATH) {
		string mergedId = read1.id.substr(0, read1.id.find_last_of("/"));
		for (unsigned i = 0; i < pathsFound.size(); i++) {
			FastaRecord mergedSeq;
			mergedSeq.id = mergedId;
			mergedSeq.seq = pathToSeq(pathsFound[i]);
			mergedSeqs.push_back(mergedSeq);
		}
	}

	return result;
}

#endif
