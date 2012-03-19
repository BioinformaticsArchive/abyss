#include "AssemblyAlgorithms.h"
#include "Assembly/Options.h"
#include "Common/Options.h"
#include "FastaReader.h"
#include "FastaWriter.h"
#include "Histogram.h"
#include "ISequenceCollection.h"
#include "Log.h"
#include "Timer.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>

using namespace std;

namespace AssemblyAlgorithms
{

/** Return the kmer which are adjacent to this kmer. */
void generateSequencesFromExtension(const Kmer& currSeq,
		extDirection dir, SeqExt extension, vector<Kmer>& outseqs)
{
	vector<Kmer> extensions;
	Kmer extSeq(currSeq);
	extSeq.shift(dir);

	// Check for the existance of the 4 possible extensions
	for (int i  = 0; i < NUM_BASES; i++) {
		// Does this sequence have an extension?
		if(extension.checkBase(i))
		{
			extSeq.setLastBase(dir, i);
			outseqs.push_back(extSeq);
		}
	}
}

/** Load sequence data into the collection. */
void loadSequences(ISequenceCollection* seqCollection, string inFile)
{
	Timer timer("LoadSequences " + inFile);

	PrintDebug(0, "Reading `%s'\n", inFile.c_str());

	if (inFile.find(".kmer") != string::npos) {
		seqCollection->load(inFile.c_str());
		return;
	}

	unsigned count = 0, count_good = 0,
			 count_small = 0, count_nonACGT = 0;
	FastaReader reader(inFile.c_str(), FastaReader::KEEP_N);
	for (Sequence seq; reader >> seq;) {
		int len = seq.length();
		if (opt::kmerSize > len) {
			count_small++;
			continue;
		}

		if (opt::rank <= 0
				&& count == 0 && seqCollection->count() == 0) {
			// Detect colour-space reads.
			bool colourSpace
				= seq.find_first_of("0123") != string::npos;
			seqCollection->setColourSpace(colourSpace);
			if (colourSpace)
				puts("Colour-space assembly");
		}

		if (isalnum(seq[0])) {
			if (opt::colourSpace)
				assert(isdigit(seq[0]));
			else
				assert(isalpha(seq[0]));
		}

		bool good = seq.find_first_not_of("ACGT0123") == string::npos;
		bool discarded = true;
		for (int i = 0; i < len - opt::kmerSize + 1; i++) {
			Sequence kmer(seq, i, opt::kmerSize);
			if (good || kmer.find_first_not_of("ACGT0123")
					== string::npos) {
				seqCollection->add(Kmer(kmer));
				discarded = false;
			}
		}
		if (discarded)
			count_nonACGT++;
		else
			count_good++;

		if (++count % 100000 == 0) {
			PrintDebug(1, "Read %u reads. ", count);
			seqCollection->printLoad();
		}
		seqCollection->pumpNetwork();
	}
	assert(reader.eof());

	PrintDebug(1, "Read %u reads. ", count);
	seqCollection->printLoad();

	if (count_small > 0)
		fprintf(stderr, "warning: discarded %u reads "
				"shorter than %u bases\n",
				count_small, opt::kmerSize);
	if (reader.unchaste() > 0)
		cerr << "warning: discarded " << reader.unchaste()
			<< " unchaste reads" << endl;
	if (count_nonACGT > 0)
		fprintf(stderr, "warning: discarded %u reads "
				"containing non-ACGT characters\n", count_nonACGT);

	if (count_good == 0)
		fprintf(stderr, "warning: `%s' contains no usable sequence\n",
				inFile.c_str());
}

//
// Generate the adjacency information for each sequence in the collection
//
void generateAdjacency(ISequenceCollection* seqCollection)
{
	Timer timer("GenerateAdjacency");

	unsigned count = 0;
	unsigned numBasesSet = 0;
	for (ISequenceCollection::iterator iter = seqCollection->begin();
			iter != seqCollection->end(); ++iter) {
		if (iter->second.deleted())
			continue;

		if (++count % 1000000 == 0)
			PrintDebug(1, "Generating adjacency: %u k-mer\n", count);

		for (extDirection dir = SENSE; dir <= ANTISENSE; ++dir) {
			Kmer testSeq(iter->first);
			uint8_t adjBase = testSeq.shift(dir);
			for (int i = 0; i < NUM_BASES; i++) {
				testSeq.setLastBase(dir, i);
				if (seqCollection->setBaseExtension(
							testSeq, !dir, adjBase))
					numBasesSet++;
			}
		}
		seqCollection->pumpNetwork();
	}

	if (numBasesSet > 0)
		PrintDebug(0, "Generated %u edges\n", numBasesSet);
}

/** Remove all the extensions both from and to this sequence. */
static void removeExtensions(ISequenceCollection* seqCollection,
		const PackedSeq& seq, extDirection dir)
{
	removeExtensionsToSequence(seqCollection, seq, dir);
	seqCollection->clearExtensions(seq.first, dir);
}

/** Mark ambiguous branches and branches from palindromes for removal.
 * @return the number of branches marked
 */
unsigned markAmbiguous(ISequenceCollection* seqCollection)
{
	Timer timer(__func__);
	unsigned progress = 0;
	unsigned count = 0;
	for (ISequenceCollection::iterator iter = seqCollection->begin();
			iter != seqCollection->end(); ++iter) {
		if (iter->second.deleted())
			continue;

		if (++progress % 1000000 == 0)
			PrintDebug(1, "Splitting: %u k-mer\n", progress);

		if (iter->first.isPalindrome()) {
			seqCollection->mark(iter->first, SENSE);
			seqCollection->mark(iter->first, ANTISENSE);
			count += 2;
			continue;
		}

		for (extDirection dir = SENSE; dir <= ANTISENSE; ++dir) {
			if (iter->second.getExtension(dir).isAmbiguous()
					|| iter->first.isPalindrome(dir)) {
				seqCollection->mark(iter->first, dir);
				count++;
			}
		}
	}
	PrintDebug(0, "Marked %u ambiguous branches\n", count);
	return count;
}

/** Remove marked branches.
 * @return the number of branches removed
 */
unsigned splitAmbiguous(ISequenceCollection* pSC)
{
	Timer timer(__func__);
	unsigned count = 0;
	for (ISequenceCollection::iterator it = pSC->begin();
			it != pSC->end(); ++it) {
		if (it->second.deleted())
			continue;
		for (extDirection sense = SENSE;
				sense <= ANTISENSE; ++sense) {
			if (it->second.marked(sense)) {
				removeExtensions(pSC, *it, sense);
				count++;
			}
		}
		pSC->pumpNetwork();
	}
	PrintDebug(0, "Split %u ambiguous branches\n", count);
	return count;
}

/** Open the bubble file. */
void openBubbleFile(ofstream& out)
{
	if (opt::snpPath.empty())
		return;
	string path;
	if (opt::rank < 0) {
		path = opt::snpPath;
	} else {
		ostringstream s;
		s << "snp-" << opt::rank << ".fa";
		path = s.str();
	}
	out.open(path.c_str());
	if (!out.is_open()) {
		perror(path.c_str());
		exit(EXIT_FAILURE);
	}
}

int popBubbles(ISequenceCollection* seqCollection, ostream& out)
{
	Timer timer("PopBubbles");
	int numPopped = 0;

	// Set the cutoffs
	const unsigned int expectedBubbleSize = 2*(opt::kmerSize + 1);
	const unsigned int maxNumBranches = 3;

	for (ISequenceCollection::iterator iter = seqCollection->begin();
			iter != seqCollection->end(); ++iter) {
		if (iter->second.deleted())
			continue;

		ExtensionRecord extRec = iter->second.extension();
		for (extDirection dir = SENSE; dir <= ANTISENSE; ++dir) {
			if (extRec.dir[dir].isAmbiguous()) {
				// Found a potential bubble, examine each branch
				bool stop = false;
				
				// Create the branch group
				BranchGroup branchGroup(0, dir, maxNumBranches,
						iter->first);
				initiateBranchGroup(branchGroup, iter->first,
						extRec.dir[dir], expectedBubbleSize);

				// Disallow any further branching.
				unsigned numInitialBranches
					= branchGroup.getNumBranches();
				if (numInitialBranches <= maxNumBranches)
					branchGroup.setMaxNumBranches(numInitialBranches);
				else
					stop = true;
				
				// Iterate over the branches
				while(!stop)
				{
					size_t numBranches = branchGroup.getNumBranches();
					
					for(unsigned int j = 0; j < numBranches; ++j)
					{						
						// Get the extensions of this branch
						ExtensionRecord extRec;
						int multiplicity = -1;

						const Kmer& lastKmer
							= branchGroup.getBranch(j).getLastSeq();
						bool success = seqCollection->getSeqData(
								lastKmer, extRec, multiplicity);
						assert(success);
						(void)success;
						processBranchGroupExtension(branchGroup, j,
								lastKmer, extRec, multiplicity);
					}
					
					// At this point all branches should have the same length or one will be a noext
					branchGroup.updateStatus();
					
					// All branches have been extended one sequence, check the stop conditions
					BranchGroupStatus status = branchGroup.getStatus();
					
					// Check if a stop condition was met
					if(status == BGS_TOOLONG || status == BGS_LOOPFOUND || status == BGS_TOOMANYBRANCHES || status == BGS_NOEXT)
					{
						stop = true;
					}
					else if(status == BGS_JOINED)
					{
						static unsigned snpID;
						writeBubble(out, branchGroup, ++snpID);
						collapseJoinedBranches(seqCollection,
								branchGroup);
						numPopped++;
						stop = true;
					}
					else
					{										
						// the branch is still active, continue
						assert(status == BGS_ACTIVE);
					}
				}
			}
		}
		seqCollection->pumpNetwork();
	}

	if (numPopped > 0)
		printf("Removed %u bubbles\n", numPopped);
	return numPopped;
}

// Populate a branch group with the inital branches from a sequence
void initiateBranchGroup(BranchGroup& group, const Kmer& seq,
		const SeqExt& extension, size_t maxBubbleSize)
{
	vector<Kmer> extSeqs;
	generateSequencesFromExtension(seq, group.getDirection(), extension, extSeqs);
	assert(extSeqs.size() > 1);
	uint64_t id = 0;

	for (vector<Kmer>::iterator seqIter = extSeqs.begin();
			seqIter != extSeqs.end(); ++seqIter) {
		BranchRecord newBranch(group.getDirection(), maxBubbleSize);
		BranchRecord& addedBranch = group.addBranch(id, newBranch);
		addedBranch.addSequence(*seqIter);
		id++;
	}
}

/** Process an a branch group extension. */
bool processBranchGroupExtension(BranchGroup& group,
		size_t branchIndex, const Kmer& seq,
		ExtensionRecord extensions, int multiplicity)
{
	vector<Kmer> branchExtSeqs;
	extDirection dir = group.getDirection();
	generateSequencesFromExtension(seq, dir, extensions.dir[dir], branchExtSeqs);

	// Set the multiplicity and extensions of the request sequence.
	group.getBranch(branchIndex).setData(
			make_pair(seq, KmerData(multiplicity, extensions)));

	if(branchExtSeqs.size() == 1)
	{
		// single extension
		group.getBranch(branchIndex).addSequence(branchExtSeqs.front());
							
	}
	else if(branchExtSeqs.size() > 1)
	{
		// Start a new branch for the k-mer [1..n]
		vector<Kmer>::iterator seqIter = branchExtSeqs.begin() + 1;
		for(; seqIter != branchExtSeqs.end(); ++seqIter)
		{
			uint64_t newID = group.getNumBranches();
			
			// Start a new branch which is a duplicate of the current branch up to this point
			BranchRecord newBranch(group.getBranch(branchIndex));
			BranchRecord& addedBranch = group.addBranch(newID, newBranch);
			addedBranch.addSequence(*seqIter);
		}
		
		// Add the first sequence (index 0) to the current branch
		group.getBranch(branchIndex).addSequence(branchExtSeqs.front());
	}
	else
	{
	
		// this branch could not be extended, set a flag
		group.setNoExtension();
	}
	
	// Return whether the group is extendable
	return group.isExtendable();
}

/** Write a bubble to the specified file. */
void writeBubble(ostream& out, BranchGroup& group, unsigned id)
{
	if (opt::snpPath.empty())
		return;

	unsigned selectedIndex = group.getBranchToKeep();
	char allele = 'A';

	BranchRecord& refBranch = group.getBranch(selectedIndex);
	Sequence refContig(refBranch);
	out << '>' << id << allele++ << ' '
		<< refContig.length() << ' '
		<< refBranch.getBranchMultiplicity() << '\n'
		<< refContig.c_str() << '\n';

	unsigned numBranches = group.getNumBranches();
	for (unsigned i = 0; i < numBranches; ++i) {
		if (i == selectedIndex)
			continue;
		BranchRecord& currBranch = group.getBranch(i);
		Sequence contig(currBranch);
		out << '>' << id << allele++ << ' '
			<< contig.length() << ' '
			<< currBranch.getBranchMultiplicity() << '\n'
			<< contig.c_str() << '\n';
	}
	assert(out.good());
}

//
// Collapse joined paths into a single path
//
void collapseJoinedBranches(ISequenceCollection* seqCollection, BranchGroup& group)
{
	assert(group.isAmbiguous(seqCollection));
	// a join was found, select a branch to keep and remove the rest
	size_t selectedIndex = group.getBranchToKeep();
	
	size_t numBranches = group.getNumBranches();
	BranchRecord& refRecord = group.getBranch(selectedIndex);
	PrintDebug(5, "Popping %zu %s\n", refRecord.getLength(),
				refRecord.getFirstSeq().decode().c_str());

	for(size_t i = 0; i < numBranches; ++i)
	{
		// Skip the branch that was selected to keep
		if(i == selectedIndex)
			continue;

		BranchRecord& branch = group.getBranch(i);
		for (BranchRecord::iterator it = branch.begin();
				it != branch.end(); ++it) {
			/* As long as we're only popping simple bubbles, the
			 * sequence being removed cannot be in the reference
			 * sequence. By now, we've forgotten the multiplicity map
			 * used by BranchRecord::exists to save memory. */
			//assert(!refRecord.exists(*branchIter));
			removeSequenceAndExtensions(seqCollection, *it);
		}
	}
	assert(!group.isAmbiguous(seqCollection));
}

/**
 * Remove a k-mer and update the extension records of the k-mer that
 * extend to it.
 */
void removeSequenceAndExtensions(ISequenceCollection* seqCollection,
		const PackedSeq& seq)
{
	// This removes the reverse complement as well
	seqCollection->remove(seq.first);
	removeExtensionsToSequence(seqCollection, seq, SENSE);
	removeExtensionsToSequence(seqCollection, seq, ANTISENSE);
}

/** Remove all the extensions to this sequence. */
void removeExtensionsToSequence(ISequenceCollection* seqCollection,
		const PackedSeq& seq, extDirection dir)
{
	SeqExt extension(seq.second.getExtension(dir));
	extDirection oppDir = oppositeDirection(dir);
	Kmer testSeq(seq.first);
	uint8_t extBase = testSeq.shift(dir);
	for (int i = 0; i < NUM_BASES; i++) {
		if (extension.checkBase(i)) {
			testSeq.setLastBase(dir, i);
			seqCollection->removeExtension(testSeq, oppDir, extBase);
		}
	}
}

/** The number of k-mer that have been eroded. */
static unsigned g_numEroded;

/** Return the number of k-mer that have been eroded. */
unsigned getNumEroded()
{
	unsigned numEroded = g_numEroded;
	g_numEroded = 0;
	PrintDebug(0, "Eroded %u tips\n", numEroded);
	return numEroded;
}

/** Consider the specified k-mer for erosion.
 * @return the number of k-mer eroded, zero or one
 */
unsigned erode(ISequenceCollection* c, const PackedSeq& seq)
{
	extDirection dir;
	SeqContiguity contiguity = checkSeqContiguity(seq, dir);
	if (contiguity == SC_INVALID || contiguity == SC_CONTIGUOUS)
		return 0;

	const KmerData& data = seq.second;
	if (data.getMultiplicity() < opt::erode
			|| data.getMultiplicity(SENSE) < opt::erodeStrand
			|| data.getMultiplicity(ANTISENSE) < opt::erodeStrand) {
		removeSequenceAndExtensions(c, seq);
		g_numEroded++;
		return 1;
	} else
		return 0;
}

/** The given sequence has changed. */
static void erosionObserver(ISequenceCollection* c,
		const PackedSeq& seq)
{
	erode(c, seq);
}

//
// Erode data off the ends of the graph, one by one
//
unsigned erodeEnds(ISequenceCollection* seqCollection)
{
	Timer erodeEndsTimer("Erode");
	assert(g_numEroded == 0);
	seqCollection->attach(erosionObserver);

	for (ISequenceCollection::iterator iter = seqCollection->begin();
			iter != seqCollection->end(); ++iter) {
		erode(seqCollection, *iter);
		seqCollection->pumpNetwork();
	}

	seqCollection->detach(erosionObserver);
	return getNumEroded();
}

/** Trimming driver function */
void performTrim(ISequenceCollection* seqCollection, int start)
{
	if (opt::trimLen == 0)
		return;
	unsigned rounds = 0, total = 0;
	for (int trim = start; trim < opt::trimLen; trim *= 2) {
		rounds++;
		total += trimSequences(seqCollection, trim);
	}
	unsigned count;
	while ((count = trimSequences(seqCollection, opt::trimLen)) > 0) {
		rounds++;
		total += count;
	}
	printf("Trimmed %u branches in %u rounds\n", total, rounds);
}

/** Return the adjacency of this sequence. */
SeqContiguity checkSeqContiguity(const PackedSeq& seq,
		extDirection& outDir)
{
	if (seq.second.deleted())
		return SC_INVALID;

	bool child = seq.second.hasExtension(SENSE);
	bool parent = seq.second.hasExtension(ANTISENSE);
	if(!child && !parent)
	{
		//this sequence is completely isolated
		return SC_ISLAND;
	}
	else if(!child)
	{
		outDir = ANTISENSE;
		return SC_ENDPOINT;
	}
	else if(!parent)
	{
		outDir = SENSE;
		return SC_ENDPOINT;
	}
	else
	{
		// sequence is contiguous
		return SC_CONTIGUOUS;
	}
}

//
// Trimming (error removal) function
//
int trimSequences(ISequenceCollection* seqCollection, int maxBranchCull)
{
	Timer timer("TrimSequences");
	printf("Trimming short branches: %u\n", maxBranchCull);
	unsigned numBranchesRemoved = 0;

	for (ISequenceCollection::iterator iter = seqCollection->begin();
			iter != seqCollection->end(); ++iter) {
		extDirection dir;
		// dir will be set to the trimming direction if the sequence can be trimmed
		SeqContiguity status = checkSeqContiguity(*iter, dir);

		if(status == SC_INVALID || status == SC_CONTIGUOUS)
		{
			continue;
		}
		else if(status == SC_ISLAND)
		{
			// remove this sequence, it has no extensions
			seqCollection->mark(iter->first);
			numBranchesRemoved++;
			continue;
		}
		// Sequence is trimmable, continue

		// This is a dead-end branch, check it for removal
		BranchRecord currBranch(dir, maxBranchCull);

		Kmer currSeq = iter->first;
		while(currBranch.isActive())
		{		
			// Get the extensions for this sequence, this function populates the extRecord structure
			ExtensionRecord extRec;
			int multiplicity = -1;
			bool success = seqCollection->getSeqData(currSeq, extRec, multiplicity);
			assert(success);
			(void)success;
			
			// process the extension record and extend the current branch, this function updates currSeq on successful extension
			processLinearExtensionForBranch(currBranch, currSeq, extRec, multiplicity);
		}
		
		// The branch has ended check it for removal, returns true if it was removed
		if(processTerminatedBranchTrim(seqCollection, currBranch))
		{
			numBranchesRemoved++;
		}
		seqCollection->pumpNetwork();
	}

	unsigned numSweeped = removeMarked(seqCollection);

	if (numBranchesRemoved > 0)
		PrintDebug(0, "Trimmed %u k-mer in %u branches\n",
				numSweeped, numBranchesRemoved);
	return numBranchesRemoved;
}

//
// Process the extension for this branch for the trimming algorithm
// CurrSeq is the current sequence being inspected (the next member to be added to the branch). The extension record is the extensions of that sequence and
// multiplicity is the number of times that kmer appears in the data set
// After processing currSeq is unchanged if the branch is no longer active or else it is the generated extension
bool processLinearExtensionForBranch(BranchRecord& branch,
		Kmer& currSeq, ExtensionRecord extensions, int multiplicity)
{
	extDirection dir = branch.getDirection();
	extDirection oppDir = oppositeDirection(dir);
	
	if(branch.isTooLong())
	{
		// Check if the branch has extended past the max trim length.
		branch.terminate(BS_TOO_LONG);
		return false;
	}
	else if(branch.hasLoop())
	{
		branch.terminate(BS_LOOP);
		return false;
	} else if (extensions.dir[oppDir].isAmbiguous()) {
		// There is a reverse ambiguity to this branch, stop the branch without adding the current sequence to it
		branch.terminate(BS_AMBI_OPP);
		return false;
	}

	branch.addSequence(make_pair(currSeq,
				KmerData(multiplicity, extensions)));
	if (branch.isTooLong()) {
		branch.terminate(BS_TOO_LONG);
		return false;
	}

	if (!extensions.dir[dir].hasExtension()) {
		// no extenstion
		branch.terminate(BS_NOEXT);
		return false;
	} else if (extensions.dir[dir].isAmbiguous()) {
		// ambiguous extension
		branch.terminate(BS_AMBI_SAME);
		return false;
	} else {
		// generate the new current sequence from the extension
		vector<Kmer> newSeqs;
		generateSequencesFromExtension(currSeq, dir, extensions.dir[dir], newSeqs);
		assert(newSeqs.size() == 1);
		currSeq = newSeqs.front();
		return true;
	}
}

bool processTerminatedBranchTrim(ISequenceCollection* seqCollection, BranchRecord& branch)
{
	assert(!branch.isActive());
	if(branch.getLength() > 0 && branch.getState() != BS_TOO_LONG)
	{
		PrintDebug(5, "Trimming %zu %s\n", branch.getLength(),
					branch.getFirstSeq().decode().c_str());
		for (BranchRecord::iterator it = branch.begin();
				it != branch.end(); ++it)
			seqCollection->mark(it->first);
		return true;
	}	
	else
	{
		return false;
	}
}

/** Remove all marked k-mer.
 * @return the number of removed k-mer
 */
unsigned removeMarked(ISequenceCollection* pSC)
{
	Timer timer(__func__);
	unsigned count = 0;
	for (ISequenceCollection::iterator it = pSC->begin();
			it != pSC->end(); ++it) {
		if (it->second.deleted())
			continue;
		if (it->second.marked()) {
			removeSequenceAndExtensions(pSC, *it);
			count++;
		}
		pSC->pumpNetwork();
	}
	if (count > 0)
		PrintDebug(1, "Removed %u marked k-mer\n", count);
	return count;
}

static Sequence processTerminatedBranchAssemble(
		const BranchRecord& branch)
{
	assert(!branch.isActive());
	// The only acceptable condition for the termination of an
	// assembly is a noext or a loop.
	assert(branch.getState() == BS_NOEXT
			|| branch.getState() == BS_LOOP);
	return branch;
}

/** Assemble a contig.
 * @return the number of k-mer below the coverage threshold
 */
unsigned assembleContig(
		ISequenceCollection* seqCollection, FastaWriter* writer,
		BranchRecord& branch, unsigned id)
{
	// Assemble the contig.
	Sequence contig(processTerminatedBranchAssemble(branch));

	unsigned kmerCount = branch.calculateBranchMultiplicity();
	if (writer != NULL)
		writer->WriteSequence(contig, id, kmerCount);

	// Remove low-coverage contigs.
	float coverage = (float)kmerCount / branch.getLength();
	if (opt::coverage > 0 && coverage < opt::coverage) {
		for (BranchRecord::iterator it = branch.begin();
				it != branch.end(); ++it)
			seqCollection->remove(it->first);
		return branch.getLength();
	}
	return 0;
}

/** Assemble contigs.
 * @return the number of contigs assembled
 */
unsigned assemble(ISequenceCollection* seqCollection,
		FastaWriter* fileWriter)
{
	Timer timer("Assemble");

	unsigned kmerCount = 0;
	unsigned contigID = 0;
	unsigned assembledKmer = 0;
	unsigned lowCoverageKmer = 0;
	unsigned lowCoverageContigs = 0;

	for (ISequenceCollection::iterator iter = seqCollection->begin();
			iter != seqCollection->end(); ++iter) {
		if (iter->second.deleted())
			continue;
		kmerCount++;

		extDirection dir;
		// dir will be set to the trimming direction if the sequence can be trimmed
		SeqContiguity status = checkSeqContiguity(*iter, dir);

		assert(status != SC_INVALID);
		if (status == SC_CONTIGUOUS)
			continue;
		else if(status == SC_ISLAND)
		{
			// singleton, output
			BranchRecord currBranch(SENSE, -1);
			currBranch.addSequence(*iter);
			currBranch.terminate(BS_NOEXT);
			unsigned removed = assembleContig(seqCollection,
					fileWriter, currBranch, contigID++);
			assembledKmer += currBranch.getLength();
			if (removed > 0) {
				lowCoverageContigs++;
				lowCoverageKmer += removed;
			}
			continue;
		}
		assert(status == SC_ENDPOINT);

		// The sequence is an endpoint, begin extending it
		// Passing -1 into the branch will disable the length check
		BranchRecord currBranch(dir, -1);

		Kmer currSeq = iter->first;
		while(currBranch.isActive())
		{		
			// Get the extensions for this sequence, this function populates the extRecord structure
			ExtensionRecord extRec;
			int multiplicity = -1;
			bool success = seqCollection->getSeqData(currSeq, extRec, multiplicity);

			assert(success);
			(void)success;
			
			// process the extension record and extend the current branch, this function updates currSeq on successful extension
			processLinearExtensionForBranch(currBranch, currSeq, extRec, multiplicity);
		}
		
		if (currBranch.isCanonical()) {
			unsigned removed = assembleContig(seqCollection,
					fileWriter, currBranch, contigID++);
			assembledKmer += currBranch.getLength();
			if (removed > 0) {
				lowCoverageContigs++;
				lowCoverageKmer += removed;
			}
		}

		seqCollection->pumpNetwork();
	}

	if (opt::coverage > 0) {
		printf("Found %u k-mer in %u contigs before removing "
				"low-coverage contigs\n", assembledKmer, contigID);
		printf("Removed %u k-mer in %u low-coverage contigs\n",
				lowCoverageKmer, lowCoverageContigs);
	} else {
		assert(assembledKmer <= kmerCount);
		unsigned circularKmer = kmerCount - assembledKmer;
		if (circularKmer > 0)
			printf("%u unassembled k-mer in circular contigs\n",
					circularKmer);
		printf("Assembled %u k-mer in %u contigs\n",
				assembledKmer, contigID);
	}
	return contigID;
}

/** Return the k-mer coverage histogram. */
Histogram coverageHistogram(const ISequenceCollection& c)
{
	Histogram h;
	for (ISequenceCollection::const_iterator it = c.begin();
			it != c.end(); ++it) {
		if (it->second.deleted())
			continue;
		h.insert(it->second.getMultiplicity());
	}
	return h;
}

void determineMinimumCoverage(const Histogram& h)
{
	if (!opt::coverageHistPath.empty() && opt::rank <= 0) {
		ofstream histFile(opt::coverageHistPath.c_str());
		assert(histFile.is_open());
		histFile << h;
		assert(histFile.good());
	}

	unsigned minCov = h.firstLocalMinimum();
	if (opt::rank <= 0) {
		if (minCov == 0)
			puts("Unable to determine minimum k-mer coverage");
		else
			printf("Minimum k-mer coverage is %u\n", minCov);
	}
	if (minCov < 2)
		minCov = 2;

	if ((int)opt::erode < 0) {
		opt::erode = minCov;
		if (opt::rank <= 0)
			printf("Setting parameter e (erode) to %u\n", opt::erode);
	}
	if (opt::coverage < 0) {
		opt::coverage = minCov;
		if (opt::rank <= 0)
			printf("Setting parameter c (coverage) to %f\n",
					opt::coverage);
	}
}

};
