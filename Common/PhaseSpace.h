#ifndef PHASESPACE_H
#define PHASESPACE_H

#include <vector>
#include <map>
#include <stdio.h>
#include "Sequence.h"
#include "HitRecord.h"
#include "CommonDefs.h"

typedef std::map<Sequence, int> BinItem;
typedef std::vector<BinItem> Bin1D;
typedef std::vector<Bin1D> Bin2D;
typedef std::vector<Bin2D> Bin3D;
typedef std::vector<Bin3D> Bin4D;

struct Coord4
{
	int x;
	int y;
	int z;
	int w;
};

class PhaseSpace
{
	public:
		PhaseSpace(int readLength);
		
		// add many sequences
		void addReads(const SequenceVector& vec);
		
		// add a sequence to a coordinate
		void addSequence(const Sequence& seq, const Coord4& c);
		
		// get the multiplicity of the sequence
		int getMultiplicity(const Sequence& seq, const Coord4& c);
		
		// check if a sequence exists
		bool checkForSequence(const Sequence& seq) const;
		
		// calculate whether this sequence has an extension in the phase space
		HitRecord calculateExtension(const Sequence& currSeq, extDirection dir) const;		
		
		// does this sequence extend from a different node?
		bool hasParent(const Sequence& seq);

		// does this sequence have an extension?
		bool hasChild(const Sequence& seq);
		
		// print everything
		void printAll() const;
		
		// compute the coordinate of a sequence
		static Coord4 SequenceToCoord4(const Sequence& seq);
	
	private:
		PhaseSpace();
		Bin4D m_phaseSpace;
};

#endif
