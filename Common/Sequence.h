#ifndef SEQUENCE_H
#define SEQUENCE_H

#include <string>
#include <vector>

enum SequenceAdjacency
{
	SA_NONE,
	SA_SAME_SENSE,
	SA_SAME_ANTISENSE,
	SA_RC_SENSE,
	SA_RC_ANTISENSE	
};

const int SEQUENCE_ID_LENGTH = 256;

typedef std::string Sequence;

char complementBaseChar(char base);
Sequence reverseComplement(const Sequence& s);

// append a base to the string
void seqAppend(Sequence& s, const std::string& str);
void seqAppendBase(Sequence& s, const char b);
void seqPrependBase(Sequence& s, const char b);

typedef std::vector<Sequence> SequenceVector;
typedef SequenceVector::iterator SequenceVectorIterator;

#endif
