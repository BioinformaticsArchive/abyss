#ifndef ISEQUENCECOLLECTION_H
#define ISEQUENCECOLLECTION_H

#include "config.h"
#include "PackedSeq.h"

#if HAVE_GOOGLE_SPARSE_HASH_SET
# include <google/sparse_hash_set>
typedef google::sparse_hash_set<PackedSeq,
		PackedSeqHasher, PackedSeqEqual> SequenceDataHash;
#else
# include "HashSet.h"
typedef hash_set<PackedSeq,
		PackedSeqHasher, PackedSeqEqual> SequenceDataHash;
#endif

typedef SequenceDataHash::iterator SequenceCollectionHashIter;
typedef SequenceDataHash::const_iterator ConstSequenceCollectionHashIter;

typedef std::pair<SequenceCollectionHashIter, SequenceCollectionHashIter> SequenceHashIterPair;
typedef SequenceCollectionHashIter SequenceCollectionIterator;
// Interface class for a sequence collection (the lowest level of storage of a large number of sequences)
// This pure virtual class defines the minimum set of functions a sequence collection must provide

// Most operations are performed on the forward and reverse reads simulatenously, this structure holds the result of such operations
struct ResultPair
{
	bool forward;
	bool reverse;
};

class ISequenceCollection
{
	public:
		virtual ~ISequenceCollection() {};
				
		// add a sequence to the collection
		virtual void add(const PackedSeq& seq) = 0;
		
		// remove a sequence from the collection
		virtual void remove(const PackedSeq& seq) = 0;
				
		// end the data load and make the sequence space ready for data read
		virtual void finalize() = 0;
		
		// check if a sequence exists
		virtual bool exists(const PackedSeq& seq) = 0;

		// Set flag for sequence seq
		virtual void setFlag(const PackedSeq& seq, SeqFlag flag) = 0;
		
		// Find if this sequence has the specified flag set
		virtual bool checkFlag(const PackedSeq& seq, SeqFlag flag) = 0;
		
		// does this sequence extend from a different node?
		virtual bool hasParent(const PackedSeq& seq) = 0;

		// does this sequence have an extension?
		virtual bool hasChild(const PackedSeq& seq) = 0;
		
		// get the multiplicity of the sequence
		virtual int getMultiplicity(const PackedSeq& seq) = 0;
		
		// Return the number of sequences in the collection
		virtual int count() const = 0;
		
		// Clear the specified flag from every sequence in the collection
		virtual void wipeFlag(SeqFlag flag) = 0;
		
		// remove the extension to the sequence
		virtual bool removeExtension(const PackedSeq& seq, extDirection dir, char base) = 0;
		
		// remove all the extensions of this sequence
		virtual void clearExtensions(const PackedSeq& seq, extDirection dir) = 0;
		
		// add an extension to the sequence
		virtual void setExtension(const PackedSeq& seq, extDirection dir, SeqExt extension) = 0;
		
		// set a single base extension
		virtual bool setBaseExtension(const PackedSeq& seq, extDirection dir, char base) = 0;
		
		// get the extension for a sequence
		virtual bool getSeqData(const PackedSeq& seq, ExtensionRecord& extRecord, int& multiplicity) = 0;
		
		// check if the extension exists
		virtual ResultPair checkExtension(const PackedSeq& seq, extDirection dir, char base) = 0;

		// Receive and dispatch packets if necessary.
		virtual unsigned pumpNetwork() = 0;

		virtual SequenceCollectionIterator getStartIter() const = 0;
		virtual SequenceCollectionIterator getEndIter() const = 0;

		// Observer pattern
		typedef void (*SeqObserver)(ISequenceCollection* c,
				const PackedSeq& seq);
		virtual void attach(SeqObserver f) = 0;
		virtual void detach(SeqObserver f) = 0;
};

#endif
