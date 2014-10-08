#ifndef PAIREDDBG_ASSEMBLYALGORITHMS_H
#define PAIREDDBG_ASSEMBLYALGORITHMS_H 1

#include "Assembly/Options.h"
#include "BranchGroup.h"
#include "BranchRecord.h"
#include "FastaWriter.h"
#include "SequenceCollection.h"
#include "Common/Log.h"
#include "Common/Timer.h"
#include <ostream>
#include <vector>
#include <string>
#if _SQL
#include "Common/InsOrderedMap.h"
#endif

class Histogram;

/** A summary of the in- and out-degree of a vertex. */
enum SeqContiguity
{
	SC_ISLAND, // sequence is completely isolated
	SC_ENDPOINT, // one end of the sequence is open
	SC_CONTIGUOUS // the sequence is closed on both ends
};

/** De Bruijn graph assembly algorithms. */
namespace AssemblyAlgorithms {

#if _SQL
extern std::vector<size_t> tempCounter;
extern InsOrderedMap<std::string,int> tempStatMap;
extern void addToDb(const std::string&, const int&);
#endif

// Read a sequence file and load them into the collection
static inline
void loadSequences(ISequenceCollection* seqCollection,
		std::string inFile);

/** Generate the adjacency information for all the sequences in the
 * collection. This is required before any other algorithm can run.
 */
static inline
size_t generateAdjacency(ISequenceCollection* seqCollection);

static inline
Histogram coverageHistogram(const ISequenceCollection& c);

static inline
void setCoverageParameters(const Histogram& h);

/* Erosion. Remove k-mer from the ends of blunt contigs. */
static inline
size_t erodeEnds(ISequenceCollection* seqCollection);

static inline
size_t erode(ISequenceCollection* c,
		const ISequenceCollection::value_type& seq);

static inline
size_t getNumEroded();

static inline
size_t removeMarked(ISequenceCollection* pSC);

// Check whether a sequence can be trimmed
static inline
SeqContiguity checkSeqContiguity(
		const ISequenceCollection::value_type& seq,
		extDirection& outDir, bool considerMarks = false);

// process a terminated branch for trimming
static inline
bool processTerminatedBranchTrim(
		ISequenceCollection* seqCollection, BranchRecord& branch);

static inline
bool extendBranch(BranchRecord& branch, KmerPair& kmer, DinucSet ext);

// Process the extensions of the current sequence for trimming
static inline
bool processLinearExtensionForBranch(BranchRecord& branch,
		KmerPair& currSeq, DinucSetPair extensions, int multiplicity,
		unsigned maxLength, bool addKmer = true);

/** Populate the branch group with the initial extensions to this
 * sequence. */
static inline
void initiateBranchGroup(BranchGroup& group, const KmerPair& seq,
		const DinucSet& extension);

// process an a branch group extension
static inline
bool processBranchGroupExtension(BranchGroup& group,
		size_t branchIndex, const KmerPair& seq,
		DinucSetPair extensions, int multiplicity,
		unsigned maxLength);

static inline
void openBubbleFile(std::ofstream& out);

static inline
void writeBubble(std::ostream& out, const BranchGroup& group,
		unsigned id);

static inline
void collapseJoinedBranches(
		ISequenceCollection* seqCollection, BranchGroup& group);

/* Split the remaining ambiguous nodes to allow for a non-redundant
 * assembly. Remove extensions to/from ambiguous sequences to avoid
 * generating redundant/wrong contigs.
 */
static inline
size_t markAmbiguous(ISequenceCollection* seqCollection);

static inline
size_t splitAmbiguous(ISequenceCollection* seqCollection);

static inline
size_t assembleContig(ISequenceCollection* seqCollection,
		FastaWriter* writer, BranchRecord& branch, unsigned id);

static inline
void removeSequenceAndExtensions(ISequenceCollection* seqCollection,
		const ISequenceCollection::value_type& seq);

static inline
void removeExtensionsToSequence(ISequenceCollection* seqCollection,
		const ISequenceCollection::value_type& seq, extDirection dir);

static inline
void generateSequencesFromExtension(const KmerPair& currSeq,
		extDirection dir, DinucSet extension,
		std::vector<KmerPair>& outseqs);

/* Non-distributed graph algorithms. */

static inline
void performTrim(SequenceCollectionHash* seqCollection);

static inline
size_t popBubbles(SequenceCollectionHash* pSC, std::ostream& out);

static inline
size_t assemble(SequenceCollectionHash* seqCollection,
		FastaWriter* fileWriter = NULL);

/** Return the kmer which are adjacent to this kmer. */
static inline
void generateSequencesFromExtension(const KmerPair& currSeq,
		extDirection dir, DinucSet extension, std::vector<KmerPair>& outseqs)
{
	std::vector<KmerPair> extensions;
	KmerPair extSeq(currSeq);
	extSeq.shift(dir);

	// Check for the existance of the 4 possible extensions
	for (unsigned i = 0; i < DinucSet::NUM_EDGES; i++) {
		// Does this sequence have an extension?
		if(extension.checkBase(i))
		{
			extSeq.setLastBase(dir, i);
			outseqs.push_back(extSeq);
		}
	}
}

} // namespace AssemblyAlgorithms

#if 0
#include "AdjacencyAlgorithm.h"
#include "AssembleAlgorithm.h"
#include "BubbleAlgorithm.h"
#include "CoverageAlgorithm.h"
#include "ErodeAlgorithm.h"
#include "LoadAlgorithm.h"
#include "SplitAlgorithm.h"
#include "TrimAlgorithm.h"
#endif

#endif
