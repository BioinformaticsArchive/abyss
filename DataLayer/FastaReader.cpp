#include "FastaReader.h"

FastaReader::FastaReader(const char* filename)
{	
	m_fileHandle.open(filename);
	assert(m_fileHandle.is_open());
}

FastaReader::~FastaReader()
{	
	m_fileHandle.close();
	assert(!m_fileHandle.is_open());
}


Sequence FastaReader::ReadSequence()
{
	char headerBuffer[MAX_FASTA_LINE];
	char seqBuffer[MAX_FASTA_LINE];	
	char id[SEQUENCE_ID_LENGTH];
	
	// make sure the file is readable
	assert(m_fileHandle.is_open());

	// read in the header
	m_fileHandle.getline(headerBuffer, MAX_FASTA_LINE);

	// read in the sequence
	m_fileHandle.getline(seqBuffer, MAX_FASTA_LINE);
			
	// parse the header
	if(sscanf(headerBuffer, ">%s %*s", id) != 1)
	{
		printf("invalid header format, read failed\n");
		assert(false);
	}
	
	return Sequence(seqBuffer);	
}

// Read in a group of sequences and return whether there are sequences remaining
bool FastaReader::ReadSequences(PSequenceVector& outseqs)
{
	outseqs.push_back(ReadSequence());	
	return isGood();

}

bool FastaReader::isGood()
{
	return !(m_fileHandle.eof() || m_fileHandle.peek() == EOF);
}
