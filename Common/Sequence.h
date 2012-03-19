#ifndef SEQUENCE_H
#define SEQUENCE_H 1

#include <stdint.h>
#include <string>
#include <vector>

typedef std::string Sequence;

Sequence reverseComplement(const Sequence& s);
Sequence colourToNucleotideSpace(char anchor, const Sequence& seq);
char colourToNucleotideSpace(char anchor, char cs);
char nucleotideToColourSpace(char a, char b);

// Create the two bit code for the base
uint8_t baseToCode(char base);
char codeToBase(uint8_t code);

typedef std::vector<Sequence> SequenceVector;
typedef SequenceVector::iterator SequenceVectorIterator;

#endif
