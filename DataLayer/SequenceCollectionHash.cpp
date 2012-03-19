#include "SequenceCollectionHash.h"
#include "Log.h"
#include "Options.h"
#include "Timer.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>

bool PackedSeqEqual::operator()(const PackedSeq& obj1, const PackedSeq& obj2) const
{
	//extern bool turnOnPrint;
	//if(turnOnPrint)	
	//	printf("comp %s == %s ? %d\n", obj1.decode().c_str(), obj2.decode().c_str(), obj1 == obj2);
	return obj1 == obj2;
}

size_t PackedSeqHasher::operator()(const PackedSeq& myObj) const 
{
	int code = myObj.getHashCode();
  	//printf("hash: %d\n", code);
	return code;
	//return 1;
}

//
// 
//
SequenceCollectionHash::SequenceCollectionHash()
	: m_seqObserver(NULL), m_adjacencyLoaded(false)
{
#if HAVE_GOOGLE_SPARSE_HASH_SET
	// sparse_hash_set uses 2.67 bits per element on a 64-bit
	// architecture and 2 bits per element on a 32-bit architecture.
	// The number of elements is rounded up to a power of two.
	if (opt::rank >= 0) {
		// Make room for 100 million k-mers. Approximately 58 million
		// k-mers fit into 2 GB of ram.
		m_pSequences = new SequenceDataHash(100000000);
	} else {
		// Allocate a big hash for a single processor.
		m_pSequences = new SequenceDataHash(1<<29);
		m_pSequences->max_load_factor(0.4);
	}

	/* sparse_hash_set requires you call set_deleted_key() before
	* calling erase().
	* See http://google-sparsehash.googlecode.com
	* /svn/trunk/doc/sparse_hash_set.html#4
	*/
	PackedSeq deleted_key;
	memset(&deleted_key, 0, sizeof deleted_key);
	m_pSequences->set_deleted_key(deleted_key);
#else
	m_pSequences = new SequenceDataHash();
#endif
}

//
// Destructor
//
SequenceCollectionHash::~SequenceCollectionHash()
{
	delete m_pSequences;
	m_pSequences = 0;
}

//
// Add a single read to the SequenceCollection
//
void SequenceCollectionHash::add(const PackedSeq& seq)
{	
	// Check if the sequence exists or reverse complement exists
	SequenceHashIterPair iters = GetSequenceIterators(seq);

	// If it exists of the reverse complement exists, do not add
	if(iters.first != m_pSequences->end() || iters.second != m_pSequences->end())
	{
		if(iters.first != m_pSequences->end())
		{
			const_cast<PackedSeq&>(*iters.first).addMultiplicity(
					SENSE);
		}
		else if(iters.second != m_pSequences->end())
		{
			const_cast<PackedSeq&>(*iters.second).addMultiplicity(
					ANTISENSE);
		}
	}
	else
	{
		m_pSequences->insert(seq);
	}
}

/** Remove the specified sequence if it exists. */
void SequenceCollectionHash::remove(const PackedSeq& seq)
{
	setFlag(seq, SF_DELETE);
}

/** Clean up by erasing sequences flagged as deleted.
 * @return the number of sequences erased
 */
unsigned SequenceCollectionHash::cleanup()
{
	Timer(__func__);
	unsigned count = 0;
	for (SequenceCollectionHashIter it = m_pSequences->begin();
			it != m_pSequences->end();) {
		if (it->isFlagSet(SF_DELETE)) {
			m_pSequences->erase(it++);
			count++;
		} else
			++it;
	}
	return count;
}

/** Return the complement of the specified base.
 * If the assembly is in colour space, this is a no-op.
 */
static inline uint8_t complementBaseCode(uint8_t base)
{
	return opt::colourSpace ? base : ~base & 0x3;
}

// Set a single base extension
bool SequenceCollectionHash::setBaseExtension(
		const PackedSeq& seq, extDirection dir, uint8_t base)
{
	SequenceHashIterPair iters = GetSequenceIterators(seq);
	bool baseSet = false;
	baseSet = baseSet || setBaseExtensionByIter(iters.first, dir, base);
	baseSet = baseSet || setBaseExtensionByIter(iters.second,
			oppositeDirection(dir), complementBaseCode(base));
	return baseSet;
}

bool SequenceCollectionHash::setBaseExtensionByIter(
		SequenceCollectionHashIter& seqIter, extDirection dir,
		uint8_t base)
{
	if(seqIter != m_pSequences->end())
	{
		const_cast<PackedSeq&>(*seqIter).setBaseExtension(dir, base);
		return true;
		//seqIter->printExtension();
	}	
	return false;
}

/** Remove the specified extension from the specified sequence if it
 * exists in this collection.
 * @return true if the specified sequence exists and false otherwise
 */
bool SequenceCollectionHash::removeExtension(const PackedSeq& seq,
		extDirection dir, uint8_t base)
{
	SequenceHashIterPair iters = GetSequenceIterators(seq);
	if (iters.first == m_pSequences->end()
			&& iters.second == m_pSequences->end())
		return false;

	removeExtensionByIter(iters.first, dir, base);
	removeExtensionByIter(iters.second,
			oppositeDirection(dir), complementBaseCode(base));

	notify(getSeqAndData(iters));
	return true;
}

void SequenceCollectionHash::removeExtensionByIter(SequenceCollectionHashIter& seqIter, extDirection dir, uint8_t base)
{
	if(seqIter != m_pSequences->end())
	{
		const_cast<PackedSeq&>(*seqIter).clearExtension(dir, base);		
	}
}


//
// Clear the extensions for this sequence
//
void SequenceCollectionHash::clearExtensions(const PackedSeq& seq, extDirection dir)
{
	SequenceHashIterPair iters = GetSequenceIterators(seq);
	clearExtensionsByIter(iters.first, dir);
	clearExtensionsByIter(iters.second, oppositeDirection(dir));	
}

//
//
//
void SequenceCollectionHash::clearExtensionsByIter(SequenceCollectionHashIter& seqIter, extDirection dir)
{
	if(seqIter != m_pSequences->end())
	{
		const_cast<PackedSeq&>(*seqIter).clearAllExtensions(dir);	
		//seqIter->printExtension();
	}
}

void SequenceCollectionHash::setFlag(const PackedSeq& seq, SeqFlag flag)
{
	SequenceHashIterPair iters = GetSequenceIterators(seq);
	setFlagByIter(iters.first, flag);
	setFlagByIter(iters.second, flag);
}

//
//
//
void SequenceCollectionHash::setFlagByIter(SequenceCollectionHashIter& seqIter, SeqFlag flag)
{
	if(seqIter != m_pSequences->end())
	{
		const_cast<PackedSeq&>(*seqIter).setFlag(flag);
	}
}

bool SequenceCollectionHash::checkFlag(const PackedSeq& seq,
		SeqFlag flag) const
{
	SequenceHashIterPair seqIters = GetSequenceIterators(seq);
	return checkFlagByIter(seqIters.first, flag)
		|| checkFlagByIter(seqIters.second, flag);
}

bool SequenceCollectionHash::checkFlagByIter(
		SequenceCollectionHashIter& seqIter, SeqFlag flag) const
{
	// Check whether the sequence and its reverse complement both have the flag set/unset
	// They SHOULD be the same and the assert will guarentee this
	if(seqIter != m_pSequences->end())
	{
		return seqIter->isFlagSet(flag);
	}
	else
	{
		return false;
	}
}

void SequenceCollectionHash::wipeFlag(SeqFlag flag)
{
	SequenceCollectionHashIter endIter = getEndIter();
	for(SequenceCollectionHashIter iter = getStartIter(); iter != endIter; ++iter)
	{
		const_cast<PackedSeq&>(*iter).clearFlag(flag);
	}
}

/** Print the load of the hash table. */
void SequenceCollectionHash::printLoad() const
{
	size_t size = m_pSequences->size();
	size_t buckets = m_pSequences->bucket_count();
	PrintDebug(1, "Hash load: %zu / %zu = %f\n",
			size, buckets, (float)size / buckets);
}

bool SequenceCollectionHash::hasParent(const PackedSeq& seq)
{
	SequenceHashIterPair iters = GetSequenceIterators(seq);
	bool forwardFlag = hasParentByIter(iters.first);
	bool reverseFlag = hasChildByIter(iters.second);
	
	// assert that the sequence and its reverse complement have identical flags
	//assert(forwardFlag == reverseFlag);
	return (forwardFlag || reverseFlag);
}

//
//
//
bool SequenceCollectionHash::hasParentByIter(SequenceCollectionHashIter seqIter) const
{
	if(seqIter != m_pSequences->end())
	{
		return seqIter->hasExtension(ANTISENSE);
	}
	else
	{
		return false;
	}
}

//
//
//
bool SequenceCollectionHash::hasChild(const PackedSeq& seq)
{
	SequenceHashIterPair iters = GetSequenceIterators(seq);
	bool forwardFlag = hasChildByIter(iters.first);
	bool reverseFlag = hasParentByIter(iters.second);

	// assert that the sequence and its reverse complement have identical flags
	//assert(forwardFlag == reverseFlag);
	return (forwardFlag || reverseFlag);
}

//
//
//
bool SequenceCollectionHash::hasChildByIter(SequenceCollectionHashIter seqIter) const
{
	if(seqIter != m_pSequences->end())
	{
		return seqIter->hasExtension(SENSE);
	}
	else
	{
		return false;
	}
}

// get the extensions and multiplicity information for a sequence
bool SequenceCollectionHash::getSeqData(const PackedSeq& seq,
		ExtensionRecord& extRecord, int& multiplicity) const
{
	SequenceHashIterPair iters = GetSequenceIterators(seq);
	bool found = false;
	if(iters.first != m_pSequences->end())
	{
		// seq found
		extRecord.dir[SENSE] = getExtensionByIter(iters.first, SENSE);
		extRecord.dir[ANTISENSE] = getExtensionByIter(iters.first, ANTISENSE);
		multiplicity = iters.first->getMultiplicity();
		found = true;
	}
	else if(iters.second != m_pSequences->end())
	{
		// reverse seq found, swap the order of the extensions and complement them (to make sure they are in the direction of the requested seq)
		extRecord.dir[SENSE] = getExtensionByIter(iters.second, ANTISENSE).complement();
		extRecord.dir[ANTISENSE] = getExtensionByIter(iters.second, SENSE).complement();
		multiplicity = iters.second->getMultiplicity();
		found = true;
	}

	return found;
}

//
//
//
SeqExt SequenceCollectionHash::getExtensionByIter(SequenceCollectionHashIter& seqIter, extDirection dir) const
{
	SeqExt ret;
	if(seqIter != m_pSequences->end())
	{
		// Sequence found
		ret = seqIter->getExtension(dir);
	}
	
	return ret;	
}

//
// Return the number of sequences held in the collection
// Note: some sequences will be marked as DELETE and will still be counted
//
size_t SequenceCollectionHash::count() const
{
	return m_pSequences->size();
}

//
// Get the iterators pointing to the sequence and its reverse complement
//
SequenceHashIterPair SequenceCollectionHash::GetSequenceIterators(const PackedSeq& seq) const
{
	SequenceHashIterPair iters;
	iters.first = FindSequence(seq);
	iters.second = FindSequence(reverseComplement(seq));
	return iters;
}

/** Return the sequence and data of the specified iterator pair. */
const PackedSeq& SequenceCollectionHash::getSeqAndData(
		const SequenceHashIterPair& iters) const
{
	if (iters.first != m_pSequences->end())
		return *iters.first;
	if (iters.second != m_pSequences->end())
		return *iters.second;
	assert(false);
	exit(EXIT_FAILURE);
}

/** Return the sequence and data of the specified key.
 * The key sequence may not contain data. The returned sequence will
 * contain data.
 */
const PackedSeq& SequenceCollectionHash::getSeqAndData(
		const PackedSeq& key) const
{
	SequenceCollectionHashIter i = FindSequence(key);
	if (i != m_pSequences->end())
		return *i;
	i = FindSequence(reverseComplement(key));
	if (i != m_pSequences->end())
		return *i;
	assert(false);
	exit(EXIT_FAILURE);
}

//
// Get the iterator pointing to the sequence
//
SequenceCollectionHashIter SequenceCollectionHash::FindSequence(const PackedSeq& seq) const
{
	return m_pSequences->find(seq);
}


//
// Get the iterator pointing to the first sequence in the bin
//
SequenceCollectionHashIter SequenceCollectionHash::getStartIter() const
{
	return m_pSequences->begin();
}

//
// Get the iterator pointing to the last sequence in the bin
//
SequenceCollectionHashIter SequenceCollectionHash::getEndIter() const
{
	return m_pSequences->end();
}

#include "Options.h"
#include <cstdio>

/** Write this collection to disk. */
void SequenceCollectionHash::store() const
{
#if HAVE_GOOGLE_SPARSE_HASH_SET
	char path[100];
	snprintf(path, sizeof path, "checkpoint-%03u.kmer", opt::rank);
	FILE* f = fopen(path, "w");
	if (f == NULL) {
		perror(path);
		exit(EXIT_FAILURE);
	}
	m_pSequences->resize(0); // Shrink the hash table.
	m_pSequences->write_metadata(f);
	m_pSequences->write_nopointer_data(f);
	fclose(f);
#else
	// Not supported.
	assert(false);
	exit(EXIT_FAILURE);
#endif
}

/** Load this collection from disk. */
void SequenceCollectionHash::load(const char* path)
{
#if HAVE_GOOGLE_SPARSE_HASH_SET
	FILE* f = fopen(path, "r");
	if (f == NULL) {
		perror(path);
		exit(EXIT_FAILURE);
	}
	m_pSequences->read_metadata(f);
	m_pSequences->read_nopointer_data(f);
	fclose(f);
	m_adjacencyLoaded = true;
#else
	(void)path;
	// Not supported.
	assert(false);
	exit(EXIT_FAILURE);
#endif
}

/** Indicate that this is a colour-space collection. */
void SequenceCollectionHash::setColourSpace(bool flag)
{
	if (m_pSequences->size() > 0)
		assert(opt::colourSpace == flag);
	opt::colourSpace = flag;
}
