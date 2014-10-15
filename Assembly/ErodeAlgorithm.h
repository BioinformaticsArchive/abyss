#ifndef ASSEMBLY_ERODE_ALGORITHM
#define ASSEMBLY_ERODE_ALGORITHM 1

namespace AssemblyAlgorithms {

/** The number of k-mer that have been eroded. */
extern size_t g_numEroded;

/**
 * Remove a k-mer and update the extension records of the k-mer that
 * extend to it.
 */
static inline
void removeSequenceAndExtensions(ISequenceCollection* seqCollection,
		const ISequenceCollection::value_type& seq)
{
	// This removes the reverse complement as well
	seqCollection->remove(seq.first);
	removeExtensionsToSequence(seqCollection, seq, SENSE);
	removeExtensionsToSequence(seqCollection, seq, ANTISENSE);
}

/** Remove all the extensions to this sequence. */
static inline
void removeExtensionsToSequence(ISequenceCollection* seqCollection,
		const ISequenceCollection::value_type& seq, extDirection dir)
{
	SeqExt extension(seq.second.getExtension(dir));
	Kmer testSeq(seq.first);
	uint8_t extBase = testSeq.shift(dir);
	for (unsigned i = 0; i < NUM_BASES; i++) {
		if (extension.checkBase(i)) {
			testSeq.setLastBase(dir, i);
			seqCollection->removeExtension(testSeq, !dir, extBase);
		}
	}
}

/** Return the number of k-mer that have been eroded. */
static inline
size_t getNumEroded()
{
	size_t numEroded = g_numEroded;
	g_numEroded = 0;
#if _SQL
	tempCounter[0] += numEroded;
#endif
	logger(0) << "Eroded " << numEroded << " tips.\n";
	return numEroded;
}

/** Consider the specified k-mer for erosion.
 * @return the number of k-mer eroded, zero or one
 */
static inline
size_t erode(ISequenceCollection* c,
		const ISequenceCollection::value_type& seq)
{
	if (seq.second.deleted())
		return 0;
	extDirection dir;
	SeqContiguity contiguity = checkSeqContiguity(seq, dir);
	if (contiguity == SC_CONTIGUOUS)
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
static inline
void erosionObserver(ISequenceCollection* c,
		const ISequenceCollection::value_type& seq)
{
	erode(c, seq);
}

//
// Erode data off the ends of the graph, one by one
//
static inline
size_t erodeEnds(ISequenceCollection* seqCollection)
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

} // namespace AssemblyAlgorithms

#endif
