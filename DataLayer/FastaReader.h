#ifndef FASTAREADER_H
#define FASTAREADER_H

#include "IFileReader.h"
#include "Sequence.h"
#include <fstream>

class FastaReader : public IFileReader
{
	public:
	
		// Constructor opens file
		FastaReader(const char* filename);
		
		// Destructor closes it
		~FastaReader();
		
		// Read a single sequence from the file
		Sequence ReadSequence();
		
		// Read sequences into the vector as packed seqs
		// Returns true unless eof has been reached
		virtual bool ReadSequences(SequenceVector& outseqs);
		
		// Returns true unless eof has been reached
		bool isGood();
				
		// Returns the number of sequences containing non-ACGT
		// characters.
		virtual unsigned getNonACGT() { return m_nonacgt; }

	private:

		std::ifstream m_fileHandle;
		unsigned m_nonacgt;
		const char* m_inPath;
};

#endif //FASTAREADER_H
