#ifndef VISITALGORITHMS_H
#define VISITALGORITHMS_H

#include "CommonDefs.h"
#include "FastaWriter.h"
#include "PackedSeq.h"
#include "PairRecord.h"
#include "ContigData.h"
#include "AlignmentCache.h"
#include "DirectedGraph.h"
#include "Stats.h"


//
// Functor objects that can be passed into generic classes
//

struct PairedResolvePolicy
{
	PairedResolvePolicy(const PDF& pairedPdf);
	bool isResolved(const ContigID& id1, ContigID& id2, size_t distance) const;
	
	// The upper and lower bound distances for a pair of sequences to be considered resolved
	size_t m_lowerLimit;
	size_t m_upperLimit;
};

struct ContigDataFunctions
{
	ContigDataFunctions(size_t k, PairedResolvePolicy* pResolvePolicy, AlignmentCache* pAliDB) : m_kmer(k), m_pResolvePolicy(pResolvePolicy), m_pDatabase(pAliDB) { m_overlap = m_kmer - 1; }
	
	
	void merge(const ContigID& targetKey, ContigData& targetData, const ContigID& slaveKey, ContigData& slaveData, extDirection dir, bool reverse, bool removeChild, bool usableMerge);
	void deleteCallback(const ContigID& slaveKey, const ContigData& slaveData);
	
	bool check(ContigData& target, const ContigData& slave, extDirection dir, bool reverse);
	
	size_t m_kmer;
	PairedResolvePolicy* m_pResolvePolicy;
	AlignmentCache* m_pDatabase;
	size_t m_overlap;
	
};

struct DBGenerator
{
	DBGenerator(AlignmentCache* pDB) : m_pDatabase(pDB) { }
	void visit(const ContigID& id, const ContigData& data);
	
	AlignmentCache* m_pDatabase;
};

struct DBValidator
{
	DBValidator(AlignmentCache* pDB) : m_pDatabase(pDB) { }
	void visit(const ContigID& id, const ContigData& data);
	
	AlignmentCache* m_pDatabase;
};


struct ContigDataOutputter
{
	ContigDataOutputter(FastaWriter* pWriter) : m_pWriter(pWriter), m_numOutput(0) { }
	void visit(const ContigID& id, const ContigData& data);
	FastaWriter* m_pWriter;
	size_t m_numOutput;
};

#if 0
struct PairedMerger
{
	PairedMerger(size_t kmer, size_t maxLength, PairedResolvePolicy* pResolvePolicy, AlignmentCache* pDB) : m_kmer(kmer), m_maxlength(maxLength), m_pResolvePolicy(pResolvePolicy), m_pDatabase(pDB) { }
		
	int resolve(DirectedGraph<ContigID, ContigData>* pGraph, const ContigID id);
	
	size_t scorePairsToSet(PSequenceVector& pairs, PSeqSet& seqSet);
	
	size_t m_kmer;
	size_t m_maxlength;
	PairedResolvePolicy* m_pResolvePolicy;
	AlignmentCache* m_pDatabase;
};
#endif

struct PairAdder
{
	PairAdder(PairRecord* pPairs) : m_pPairs(pPairs) {}
	
	void visit(const ContigID& /*id*/, ContigData& data) { data.addPairs(m_pPairs); }
	PairRecord* m_pPairs;
};

struct SequenceDataCost
{
	SequenceDataCost(size_t kmer) : m_overlap(kmer - 1) { }
	
	size_t cost(const ContigData& data) { return data.getLength() - m_overlap; } 
	size_t m_overlap;
};

struct PairedDistHistogramGenerator
{
	PairedDistHistogramGenerator(Histogram* pHist, int cutoff) : m_pHist(pHist), m_distanceCutoff(cutoff) { }
	
	void visit(const ContigID& id, ContigData& data);
	
	Histogram* m_pHist;
	size_t m_distanceCutoff;

};

struct PairedResolveVisitor
{
	PairedResolveVisitor(PairedResolvePolicy* pPolicy) : m_pResolvePolicy(pPolicy) {}
	void visit(const ContigID& /*id*/, ContigData& data) { data.resolvePairs(m_pResolvePolicy, SENSE); data.resolvePairs(m_pResolvePolicy, ANTISENSE);}
	
	PairedResolvePolicy* m_pResolvePolicy;
	
};

//
// Utility functions
//
size_t scorePath(const ContigSupportMap& scoreMap, const ContigIDVec& path);
void getReachableSet(const ContigData& data, extDirection dir, ContigIDSet& idSet);
bool isJoinUnique(const ContigData& data, extDirection dir, ContigID targetID);



#endif
