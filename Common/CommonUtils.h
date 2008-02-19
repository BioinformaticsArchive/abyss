#ifndef COMMONUTILS_H
#define COMMONUTILS_H

#include "PackedSeq.h"
#include "Sequence.h"

// A myriad of functions that don't fit well into any class

// make a one base extension of this sequence
void makeExtensions(const PackedSeq& seq, extDirection dir, PSequenceVector& outVector);

// make all the one base permutations of this sequence
void makePermutations(const Sequence& seq, SequenceVector& outVector);

extDirection oppositeDirection(extDirection dir);

// calculate the information entropy of the string
double entropy(const Sequence& s);

// min/max
int min(const int& n1, const int& n2);
int max(const int& n1, const int& n2);

// complement a base
char complement(const char& b);

#endif
