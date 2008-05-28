#ifndef VISITALGORITHMS_H
#define VISITALGORITHMS_H

#include "CommonDefs.h"
#include "FastaWriter.h"
#include "PackedSeq.h"
#include "PairRecord.h"
#include "ContigData.h"
#include "AlignmentCache.h"

struct ContigDataFunctions
{
	ContigDataFunctions(size_t k /*,AlignmentCache* pAliCache*/) : m_kmer(k)/*, m_pDatabase(pAliCache)*/ { m_overlap = m_kmer - 1; }
	
	
	void merge(const ContigID& targetKey, ContigData& targetData, const ContigID& slaveKey, const ContigData& slaveData, extDirection dir, bool reverse);
	void deleteContig(const Contig& slaveKey, const ContigData& slaveData);
	
	bool check(ContigData& target, const ContigData& slave, extDirection dir, bool reverse);
	
	size_t m_kmer;
	AlignmentCache* m_pDatabase;
	size_t m_overlap;
	
};


struct ContigDataOutputter
{
	ContigDataOutputter(FastaWriter* pWriter) : m_pWriter(pWriter), m_numOutput(0) { }
	void visit(const ContigID& id, const ContigData& data);
	FastaWriter* m_pWriter;
	size_t m_numOutput;
};

struct PairedResolver
{
	PairedResolver(size_t kmer, size_t maxLength, PairRecord* pPairs) : m_kmer(kmer), m_maxlength(maxLength), m_pPairs(pPairs) { }
	
	typedef std::vector<ContigData*> ContigDataCollection;
	typedef std::vector<ContigDataCollection> ContigDataComponents;
	typedef std::vector<PSeqSet> ScoreSets;
	
	
	int resolve(const ContigData& data, extDirection dir, ContigDataComponents& components);
	size_t scorePairsToSet(PSequenceVector& pairs, PSeqSet& seqSet);
	
	size_t m_kmer;
	size_t m_maxlength;
	PairRecord* m_pPairs;
	
};

struct SequenceDataCost
{
	SequenceDataCost() { }
	
	size_t cost(const ContigData& data) { return data.seq.length(); } 
	
};

#endif
