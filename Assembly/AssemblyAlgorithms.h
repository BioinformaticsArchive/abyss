#ifndef ASSEMBLYALGORITHMS_H
#define ASSEMBLYALGORITHMS_H 1

#include "BranchGroup.h"
#include "BranchRecord.h"
#include "FastaWriter.h"
#include "ISequenceCollection.h"
#include <ostream>
#include <vector>

class Histogram;

/*********************************************************
 * 
 * AssemblyAlgorithms.h
 * 
 * A collection of functions to operate on sequence data
 * sets. These functions are designed to work with network
 * (parallel) or local (single cpu) data
 * 
 * 
 **********************************************************/
enum SeqContiguity
{
	SC_ISLAND, // sequence is completely isolated
	SC_ENDPOINT, // one end of the sequence is open 
	SC_CONTIGUOUS // the sequence is closed on both ends
};

namespace AssemblyAlgorithms
{

//
//
// Data preperation functions
//
//

// Read a sequence file and load them into the collection
void loadSequences(ISequenceCollection* seqCollection,
		std::string inFile);

// Generate the adjacency information for all the sequences in the collection
// This is required before any other algorithm can run
void generateAdjacency(ISequenceCollection* seqCollection);

Histogram coverageHistogram(const ISequenceCollection& c);
void setCoverageParameters(const Histogram& h);

/* Erosion. Remove k-mer from the ends of blunt contigs. */
unsigned erodeEnds(ISequenceCollection* seqCollection);
unsigned erode(ISequenceCollection* c, const PackedSeq& seq);
unsigned getNumEroded();

// trimming driver function, iteratively calls trimSequences to get rid of sequences that likely contain errors
void performTrim(ISequenceCollection* seqCollection, int start = 1);

// Function to perform the actual trimming. Walks the sequence space 
int trimSequences(ISequenceCollection* seqCollection, int maxBranchCull);
unsigned removeMarked(ISequenceCollection* pSC);

// Check whether a sequence can be trimmed
SeqContiguity checkSeqContiguity(const PackedSeq& seq,
		extDirection& outDir, bool considerMarks = false);

// process a terminated branch for trimming
bool processTerminatedBranchTrim(ISequenceCollection* seqCollection, BranchRecord& branch);

bool extendBranch(BranchRecord& branch, Kmer& kmer, SeqExt ext);

// Process the extensions of the current sequence for trimming
bool processLinearExtensionForBranch(BranchRecord& branch,
		Kmer& currSeq, ExtensionRecord extensions, int multiplicity,
		bool addKmer = true);

// Pop bubbles (loops of sequence that diverge a single base, caused by SNPs or consistent sequence errors
int popBubbles(ISequenceCollection* pSC, std::ostream& out);

// Populate the branch group with the initial extensions to this sequence
void initiateBranchGroup(BranchGroup& group, const Kmer& seq,
		const SeqExt& extension, size_t maxBubbleSize);

// process an a branch group extension
bool processBranchGroupExtension(BranchGroup& group,
		size_t branchIndex, const Kmer& seq,
		ExtensionRecord extensions, int multiplicity);

void openBubbleFile(std::ofstream& out);
void writeBubble(std::ostream& out, const BranchGroup& group,
		unsigned id);
void collapseJoinedBranches(ISequenceCollection* seqCollection, BranchGroup& group);

//
//
// Split the remaining ambiguous nodes to allow for a non-redundant assembly
//
//

// Remove extensions to/from ambiguous sequences to avoid generating redundant/wrong contigs
unsigned markAmbiguous(ISequenceCollection* seqCollection);
unsigned splitAmbiguous(ISequenceCollection* seqCollection);

unsigned assembleContig(ISequenceCollection* seqCollection,
		FastaWriter* writer, BranchRecord& branch, unsigned id);
unsigned assemble(ISequenceCollection* seqCollection,
		FastaWriter* fileWriter = NULL);

void removeSequenceAndExtensions(ISequenceCollection* seqCollection,
		const PackedSeq& seq);
void removeExtensionsToSequence(ISequenceCollection* seqCollection,
		const PackedSeq& seq, extDirection dir);

void generateSequencesFromExtension(const Kmer& currSeq,
		extDirection dir, SeqExt extension,
		std::vector<Kmer>& outseqs);

};

#endif
