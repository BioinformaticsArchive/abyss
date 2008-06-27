#include <stdio.h>
#include <math.h>
#include <iostream>
#include "SequenceCollectionHash.h"
#include "AssemblyAlgorithms.h"
#include "Options.h"
#include "FastaReader.h"
#include "Stats.h"
#include "PairUtils.h"
#include "PairedAlgorithms.h"


// Functions
void readContigsIntoDB(std::string refFastaFile, Aligner& aligner);
void alignReadsToDB(std::string readsFastqFile, Aligner& aligner);
void readFastqRecord(std::ifstream& stream, std::string& readID, Sequence& seq);

int main(int argc, char** argv)
{
	if(argc < 4)
	{
		std::cout << "Usage: <kmer> <reads fastq> <ref fasta>\n";
		exit(1);
	}
	
	size_t kmer = atoi(argv[1]);
	std::string readsFile(argv[2]);
	std::string refFastaFile(argv[3]);

	std::cout << "Kmer " << kmer << " Reads file: " << readsFile << " ref fasta: " << refFastaFile << std::endl;

	Aligner aligner(kmer);
	
	// Read the contigs into the ref DB
	readContigsIntoDB(refFastaFile, aligner);
	
	// Align the reads
	alignReadsToDB(readsFile, aligner);
	
	return 1;
} 

void readContigsIntoDB(std::string refFastaFile, Aligner& aligner)
{
	int count = 0;
	std::ifstream fileHandle(refFastaFile.c_str());	
	while(!fileHandle.eof() && fileHandle.peek() != EOF)
	{
		ContigID contigID;
		Sequence seq;
		int length;
		double coverage;
		
		PairedAlgorithms::parseContigFromFile(fileHandle, contigID, seq, length, coverage);
		aligner.addReferenceSequence(contigID, seq);
		
		if(count % 100000 == 0)
		{
			std::cout << "Read " << count << " contigs, " << aligner.getNumSeqs() << " seqs in the DB\n";
		}
		count++;
	}
	
	printf("Done loading reference (%zu seqs in db)\n", aligner.getNumSeqs());
	fileHandle.close();
}

void alignReadsToDB(std::string readsFastqFile, Aligner& aligner)
{
	std::ifstream fileHandle(readsFastqFile.c_str());	
	while(!fileHandle.eof() && fileHandle.peek() != EOF)
	{
		std::string readID;
		Sequence readSeq;
		
		readFastqRecord(fileHandle, readID, readSeq);
		AlignmentVector avec;
		aligner.alignRead(PackedSeq(readSeq), avec);
		
		std::cout << readID << "\t";

		for(AlignmentVector::iterator iter = avec.begin(); iter != avec.end(); ++iter)
		{
			std::cout << *iter << "\t";
		}
		
		std::cout << "\n";
	}
}

void readFastqRecord(std::ifstream& stream, std::string& readID, Sequence& seq)
{
	std::string buffer;

	// Read the header.
	getline(stream, buffer);
	assert(buffer[0] == '@');
	
	readID = buffer.substr(1);
	
	// Read the sequence.
	getline(stream, seq);

	// Read the quality values.
	getline(stream, buffer);
	assert(buffer[0] == '+');
	getline(stream, buffer);
}
