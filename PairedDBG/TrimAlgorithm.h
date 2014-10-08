#ifndef PAIREDDBG_TRIMALGORITHM_H
#define PAIREDDBG_TRIMALGORITHM_H 1

namespace AssemblyAlgorithms {

static inline
size_t trimSequences(SequenceCollectionHash* seqCollection,
		unsigned maxBranchCull);

/** Trimming driver function */
static inline
void performTrim(SequenceCollectionHash* seqCollection)
{
	if (opt::trimLen == 0)
		return;
	unsigned rounds = 0;
	size_t total = 0;
	for (unsigned trim = 1; trim < opt::trimLen; trim *= 2) {
		rounds++;
		total += trimSequences(seqCollection, trim);
	}
	size_t count;
	while ((count = trimSequences(seqCollection, opt::trimLen)) > 0) {
		rounds++;
		total += count;
	}
	std::cout << "Pruned " << total << " tips in "
		<< rounds << " rounds.\n";
#if _SQL
	tempCounter[1] += total;
	tempCounter[2] = rounds;
#endif
}

/** Return the adjacency of this sequence.
 * @param considerMarks when true, treat a marked vertex as having
 * no edges
 */
static inline
SeqContiguity checkSeqContiguity(
		const ISequenceCollection::value_type& seq,
		extDirection& outDir, bool considerMarks)
{
	assert(!seq.second.deleted());
	bool child = seq.second.hasExtension(SENSE)
		&& !(considerMarks && seq.second.marked(SENSE));
	bool parent = seq.second.hasExtension(ANTISENSE)
		&& !(considerMarks && seq.second.marked(ANTISENSE));
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

/** Prune tips shorter than maxBranchCull. */
static inline
size_t trimSequences(SequenceCollectionHash* seqCollection,
		unsigned maxBranchCull)
{
	Timer timer("TrimSequences");
	std::cout << "Pruning tips shorter than "
		<< maxBranchCull << " bp...\n";
	size_t numBranchesRemoved = 0;

	for (ISequenceCollection::iterator iter = seqCollection->begin();
			iter != seqCollection->end(); ++iter) {
		if (iter->second.deleted())
			continue;

		extDirection dir;
		// dir will be set to the trimming direction if the sequence
		// can be trimmed.
		SeqContiguity status = checkSeqContiguity(*iter, dir);

		if (status == SC_CONTIGUOUS)
			continue;
		else if(status == SC_ISLAND)
		{
			// remove this sequence, it has no extensions
			seqCollection->mark(iter->first);
			numBranchesRemoved++;
			continue;
		}

		BranchRecord currBranch(dir);
		Kmer currSeq = iter->first;
		while(currBranch.isActive())
		{
			ExtensionRecord extRec;
			int multiplicity = -1;
			bool success = seqCollection->getSeqData(
					currSeq, extRec, multiplicity);
			assert(success);
			(void)success;
			processLinearExtensionForBranch(currBranch,
					currSeq, extRec, multiplicity, maxBranchCull);
		}

		// The branch has ended check it for removal, returns true if
		// it was removed.
		if(processTerminatedBranchTrim(seqCollection, currBranch))
		{
			numBranchesRemoved++;
		}
		seqCollection->pumpNetwork();
	}

	size_t numSweeped = removeMarked(seqCollection);

	if (numBranchesRemoved > 0)
		logger(0) << "Pruned " << numSweeped << " k-mer in "
			<< numBranchesRemoved << " tips.\n";
	return numBranchesRemoved;
}

/** Extend this branch. */
static inline
bool extendBranch(BranchRecord& branch, Kmer& kmer, SeqExt ext)
{
	if (!ext.hasExtension()) {
		branch.terminate(BS_NOEXT);
		return false;
	} else if (ext.isAmbiguous()) {
		branch.terminate(BS_AMBI_SAME);
		return false;
	} else {
		std::vector<Kmer> adj;
		generateSequencesFromExtension(kmer, branch.getDirection(),
				ext, adj);
		assert(adj.size() == 1);
		kmer = adj.front();
		return true;
	}
}

/**
 * Process the extension for this branch for the trimming algorithm
 * CurrSeq is the current sequence being inspected (the next member to
 * be added to the branch). The extension record is the extensions of
 * that sequence and multiplicity is the number of times that kmer
 * appears in the data set. After processing currSeq is unchanged if
 * the branch is no longer active or else it is the generated
 * extension. If the parameter addKmer is true, add the k-mer to the
 * branch.
 */
static inline
bool processLinearExtensionForBranch(BranchRecord& branch,
		Kmer& currSeq, ExtensionRecord extensions, int multiplicity,
		unsigned maxLength, bool addKmer)
{
	/** Stop contig assembly at palindromes. */
	const bool stopAtPalindromes = !opt::ss && maxLength == UINT_MAX;

	extDirection dir = branch.getDirection();
	if (branch.isTooLong(maxLength)) {
		// Too long.
		branch.terminate(BS_TOO_LONG);
		return false;
	} else if (extensions.dir[!dir].isAmbiguous()) {
		// Ambiguous.
		branch.terminate(BS_AMBI_OPP);
		return false;
	} else if (stopAtPalindromes && currSeq.isPalindrome()) {
		// Palindrome.
		branch.terminate(BS_AMBI_SAME);
		return false;
	}

	if (addKmer)
		branch.push_back(std::make_pair(currSeq,
					KmerData(multiplicity, extensions)));

	if (branch.isTooLong(maxLength)) {
		// Too long.
		branch.terminate(BS_TOO_LONG);
		return false;
	} else if (stopAtPalindromes && currSeq.isPalindrome(dir)) {
		// Palindrome.
		branch.terminate(BS_AMBI_SAME);
		return false;
	}

	return extendBranch(branch, currSeq, extensions.dir[dir]);
}

/** Trim the specified branch if it meets trimming criteria.
 * @return true if the specified branch was trimmed
 */
static inline
bool processTerminatedBranchTrim(ISequenceCollection* seqCollection,
		BranchRecord& branch)
{
	assert(!branch.isActive());
	assert(!branch.empty());
	if (branch.getState() == BS_NOEXT
			|| branch.getState() == BS_AMBI_OPP) {
		logger(5) << "Pruning " << branch.size() << ' '
			<< branch.front().first << '\n';
		for (BranchRecord::iterator it = branch.begin();
				it != branch.end(); ++it)
			seqCollection->mark(it->first);
		return true;
	} else
		return false;
}

} // namespace AssemblyAlgorithms

#endif
