#include <algorithm>
#include "SequenceCollection.h"
#include "CommonUtils.h"

//
// Set up the 4D space to be the size of the slice passed in
//
SequenceCollection::SequenceCollection() : m_state(CS_LOADING)
{
	m_pSequences = new SequenceData;
}

//
// Destructor
//
SequenceCollection::~SequenceCollection()
{
	delete m_pSequences;
	m_pSequences = 0;
}

//
// Add a single read to the SequenceCollection
//
void SequenceCollection::add(const PackedSeq& seq)
{
	m_pSequences->push_back(seq);	
}

//
// Remove a read
//
void SequenceCollection::remove(const PackedSeq& seq)
{
	setFlag(seq, SF_DELETE);
	setFlag(reverseComplement(seq), SF_DELETE);	
}

//
// Get the IDs corresponding to this sequence
//
const IDList SequenceCollection::getIDs(const PackedSeq& seq)
{
	SequenceCollectionIter iter = FindSequence(seq);
	IDList list;
	if(iter != m_pSequences->end())
	{
		list = iter->getIDList();
	}
	return list;
}

//
// add an extension to this sequence in the record
//
void SequenceCollection::setExtension(const PackedSeq& seq, extDirection dir, SeqExt extension)
{
	SequenceIterPair iters = GetSequenceIterators(seq);
	setExtensionByIter(iters.first, dir, extension);
	setExtensionByIter(iters.second, oppositeDirection(dir), extension.complement());	
}

//
//
//
void SequenceCollection::setExtensionByIter(SequenceCollectionIter& seqIter, extDirection dir, SeqExt extension)
{
	if(seqIter != m_pSequences->end())
	{
		seqIter->setExtension(dir, extension);
		//seqIter->printExtension();
	}
}

//
// Remove the extension to this sequence from the record
//
void SequenceCollection::removeExtension(const PackedSeq& seq, extDirection dir, char base)
{
	SequenceIterPair iters = GetSequenceIterators(seq);
	removeExtensionByIter(iters.first, dir, base);
	removeExtensionByIter(iters.second, oppositeDirection(dir), complement(base));	
}

//
//
//
void SequenceCollection::removeExtensionByIter(SequenceCollectionIter& seqIter, extDirection dir, char base)
{
	if(seqIter != m_pSequences->end())
	{
		seqIter->clearExtension(dir, base);	
		//seqIter->printExtension();
	}
}

//
// Clear the extensions for this sequence
//
void SequenceCollection::clearExtensions(const PackedSeq& seq, extDirection dir)
{
	SequenceIterPair iters = GetSequenceIterators(seq);
	clearExtensionsByIter(iters.first, dir);
	clearExtensionsByIter(iters.second, oppositeDirection(dir));	
}

//
//
//
void SequenceCollection::clearExtensionsByIter(SequenceCollectionIter& seqIter, extDirection dir)
{
	if(seqIter != m_pSequences->end())
	{
		seqIter->clearAllExtensions(dir);	
		//seqIter->printExtension();
	}
}

//
// check if a sequence exists in the phase space
//
bool SequenceCollection::exists(const PackedSeq& seq)
{
	assert(m_state != CS_LOADING);
	ResultPair rp;
	
	SequenceIterPair iters = GetSequenceIterators(seq);
	rp.forward = existsByIter(iters.first);
	rp.reverse = existsByIter(iters.second);
	return (rp.forward || rp.reverse);
}

//
// Check if this sequence exists using an iterator
//
bool SequenceCollection::existsByIter(SequenceCollectionIter& seqIter) const
{
	if(seqIter != m_pSequences->end())
	{
		// sequence was found
		return !seqIter->isFlagSet(SF_DELETE);
	}
	else
	{
		return false;
	}	
}

//
//
//
void SequenceCollection::setFlag(const PackedSeq& seq, SeqFlag flag)
{
	assert(m_state == CS_FINALIZED);
	SequenceIterPair iters = GetSequenceIterators(seq);
	setFlagByIter(iters.first, flag);
	setFlagByIter(iters.second, flag);
}

//
//
//
void SequenceCollection::setFlagByIter(SequenceCollectionIter& seqIter, SeqFlag flag)
{
	assert(m_state == CS_FINALIZED);
	
	if(seqIter != m_pSequences->end())
	{
		seqIter->setFlag(flag);
	}
}

//
//
//
bool SequenceCollection::checkFlag(const PackedSeq& seq, SeqFlag flag)
{
	assert(m_state == CS_FINALIZED);
	ResultPair result;
	SequenceIterPair seqIters = GetSequenceIterators(seq);
	result.forward = checkFlagByIter(seqIters.first, flag);
	result.reverse = checkFlagByIter(seqIters.second, flag);

	//assert(forwardFlag == reverseFlag);
	return (result.forward || result.reverse);
}

//
//
//
bool SequenceCollection::checkFlagByIter(SequenceCollectionIter& seqIter, SeqFlag flag)
{
	assert(m_state == CS_FINALIZED);

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

//
//
//
void SequenceCollection::finalize()
{
	assert(m_state == CS_LOADING);
	// Sort the sequence space
	std::sort(m_pSequences->begin(), m_pSequences->end());
	
	bool checkMultiplicity = true;
	
	if(checkMultiplicity)
	{
		
		bool duplicates = checkForDuplicates();
		
		if(duplicates)
		{
			printf("duplicate sequences found, removing them\n");
			SequenceData temp;
			// copy the unique elements over
			SequenceData::iterator iter = m_pSequences->begin();
			
			// the first item is trivially unique
			temp.push_back(*iter);

			iter++;
			while(iter != m_pSequences->end())
			{
				if(*iter == temp.back())
				{
					// add iter's ids to the last sequence added to the vector (which is by definition a dup of this sequence()
					temp.back().addIDList(iter->getIDList());
				}
				else
				{
					// this item is unique
					temp.push_back(*iter);
				}
				iter++;
			}
			
			/*
			std::back_insert_iterator<SequenceData> insertIter(temp);
			std::unique_copy(m_pSequences->begin(), m_pSequences->end(), insertIter);
			*/
			// swap vectors
			temp.swap(*m_pSequences);

			printf("%d sequences remain after duplicate removal\n", count());
		}
			
	}
	m_state = CS_FINALIZED;	
}

//
//
//
bool SequenceCollection::checkForDuplicates() const
{
	assert(m_state == CS_LOADING);
	ConstSequenceCollectionIter prev = m_pSequences->begin();
	ConstSequenceCollectionIter startIter = m_pSequences->begin() + 1;
	ConstSequenceCollectionIter endIter = m_pSequences->end();
	
	bool sorted = true;
	bool duplicates = false;
	for(ConstSequenceCollectionIter iter = startIter; iter != endIter; iter++)
	{
		bool equal = false;
		if(*prev == *iter)
		{
			equal = true;
			duplicates = true;
		}
				
		if(!(*prev < *iter || equal))
		{
			sorted = false;
			printf("sort failure: %s < %s\n", prev->decode().c_str(), iter->decode().c_str());
			break;
		}
		
		//slow
		if(!(prev->decode() < iter->decode() || equal))
		{
			sorted = false;
			printf("sort failure: %s < %s\n", prev->decode().c_str(), iter->decode().c_str());
			break;
		}
		
		if(*prev == *iter)
		{
			duplicates = true;
		}
		
		prev = iter;
	}
	
	assert(sorted);
	
	return duplicates;
}

//
//
//
bool SequenceCollection::hasParent(const PackedSeq& seq)
{
	assert(m_state == CS_FINALIZED);
	SequenceIterPair iters = GetSequenceIterators(seq);
	bool forwardFlag = hasParentByIter(iters.first);
	bool reverseFlag = hasChildByIter(iters.second);
	
	// assert that the sequence and its reverse complement have identical flags
	//assert(forwardFlag == reverseFlag);
	return (forwardFlag || reverseFlag);
}

//
//
//
bool SequenceCollection::hasParentByIter(SequenceCollectionIter seqIter) const
{
	assert(m_state == CS_FINALIZED);
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
bool SequenceCollection::hasChild(const PackedSeq& seq)
{
	assert(m_state == CS_FINALIZED);
	SequenceIterPair iters = GetSequenceIterators(seq);
	bool forwardFlag = hasChildByIter(iters.first);
	bool reverseFlag = hasParentByIter(iters.second);

	// assert that the sequence and its reverse complement have identical flags
	//assert(forwardFlag == reverseFlag);
	return (forwardFlag || reverseFlag);
}

//
//
//
bool SequenceCollection::hasChildByIter(SequenceCollectionIter seqIter) const
{
	assert(m_state == CS_FINALIZED);
	if(seqIter != m_pSequences->end())
	{
		return seqIter->hasExtension(SENSE);
	}
	else
	{
		return false;
	}
}

//
//
//
ResultPair SequenceCollection::checkExtension(const PackedSeq& seq, extDirection dir, char base) const
{
	assert(m_state == CS_FINALIZED);
	ResultPair rp;
	SequenceIterPair iters = GetSequenceIterators(seq);
	
	
	rp.forward = checkExtensionByIter(iters.first, dir, base);
	rp.reverse = checkExtensionByIter(iters.second, oppositeDirection(dir), complement(base));

	return rp;
}
//
//
//
bool SequenceCollection::checkExtensionByIter(SequenceCollectionIter& seqIter, extDirection dir, char base) const
{
	if(seqIter != m_pSequences->end())
	{
		return seqIter->checkExtension(dir, base);
	}
	else
	{
		return false;
	}
}

//
// Return the number of sequences held in the collection
// Note: some sequences will be marked as DELETE and will still be counted
//
int SequenceCollection::count() const
{
	return m_pSequences->size();
}

//
// Get the iterators pointing to the sequence and its reverse complement
//
SequenceIterPair SequenceCollection::GetSequenceIterators(const PackedSeq& seq) const
{
	SequenceIterPair iters;
	iters.first = FindSequence(seq);
	iters.second = FindSequence(reverseComplement(seq));
	return iters;
}

//
// Get the iterator pointing to the sequence
//
SequenceCollectionIter SequenceCollection::FindSequence(const PackedSeq& seq) const
{
	SequenceCollectionIter iter = std::lower_bound(m_pSequences->begin(), m_pSequences->end(), seq);
	if(iter != m_pSequences->end() && *iter != seq)
	{
		iter = m_pSequences->end();
	}
	return iter;
}
#if 0
//
// Get the iterator pointing to the first sequence in the bin
//
SequenceCollectionIter SequenceCollection::getStartIter()
{
	return m_pSequences->begin();
}

//
// Get the iterator pointing to the last sequence in the bin
//
SequenceCollectionIter SequenceCollection::getEndIter()
{
	return m_pSequences->end();
}
#endif
