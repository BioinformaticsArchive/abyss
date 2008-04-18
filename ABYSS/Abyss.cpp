#include <stdio.h>

#include <vector>
#include <stdio.h>
#include <deque>
#include <iostream>
#include <fstream>
#include "Abyss.h"
#include "CommonUtils.h"
#include "DotWriter.h"
#include "SequenceCollection.h"
#include "SequenceCollectionHash.h"
#include "AssemblyAlgorithms.h"

int main(int argc, char** argv)
{	

	if(argc < 4 || argv[1] == "--help")
	{
		printUsage();
		exit(1);
	}

	std::string fastaFile = argv[1];
	int readLen = atoi(argv[2]);
	int kmerSize = atoi(argv[3]);
	
	bool noTrim = false;
	if(argc == 5)
	{
		std::string flags = argv[4];
		if(flags == "-notrim")
		{
			noTrim = true;
		}
	}

	// Load the phase space
	SequenceCollectionHash* pSC = new SequenceCollectionHash();
	//SequenceCollection* pSC = new SequenceCollection();
	
	loadSequences(pSC, fastaFile, readLen, kmerSize);

	printf("total sequences: %d\n", pSC->count());
	
	printf("finalizing\n");
	pSC->finalize();

	generateAdjacency(pSC);

	if(!noTrim)
	{
		performTrim(pSC, readLen, kmerSize);
	}
	
	outputSequences("trimmed.fa", pSC);
	
	// Remove bubbles
	popBubbles(pSC, kmerSize);

	puts("Building graph.dot...");
	ofstream dot_out("graph.dot");
	DotWriter::write(dot_out, *pSC);
	puts("Done.");

	splitAmbiguous(pSC);
	
	assemble(pSC, readLen, kmerSize);

	delete pSC;
	
	return 0;
}



void printUsage()
{
	printf("usage: ABYSS <reads fasta file> <max read length> <kmer size>\n");	
}
