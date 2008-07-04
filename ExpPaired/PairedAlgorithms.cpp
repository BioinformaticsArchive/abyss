#include "PairedAlgorithms.h"
#include "PairUtils.h"

namespace PairedAlgorithms
{
	
//
// Read contigs
//
void readContigVec(std::string file, ContigVec& outVec)
{
	std::ifstream fileHandle(file.c_str());	
	while(!fileHandle.eof() && fileHandle.peek() != EOF)
	{
		ContigID strID;
		LinearNumKey numID;
		Sequence seq;
		int length;
		double coverage;
		
		
		if(!parseContigFromFile(fileHandle, strID, seq, length, coverage))
		{
			break; //done
		}
		
		assert(!seq.empty());
		assert(length > 0);

		numID = convertContigIDToLinearNumKey(strID);
		
		Contig nc;
		
		// Make sure the index is correct
		assert(numID == outVec.size());
		nc.seq = seq;
		nc.merged = false;
		nc.repetitive = false;
		nc.super = false;
		nc.coverage = (int)coverage;
		outVec.push_back(nc);
		
	}
	fileHandle.close();
}

bool parseContigFromFile(std::ifstream& stream, ContigID& id, Sequence& seq, int& length, double& coverage)
{
	char head;

	stream >> head;		
	stream >> id;
	stream >> length;
	stream >> coverage;
	stream >> seq;
	
	return stream.good();
}

#if 0
void generateGraph(ContigGraph* pGraph, const ContigMap& contigMap, ISequenceCollection* pSC, size_t kmer, AlignmentCache* pDB)
{
	// Add all the vertices 
	for(ContigMap::const_iterator contigIter = contigMap.begin(); contigIter != contigMap.end(); ++contigIter)
	{		
		ContigData data(contigIter->first, contigIter->second.seq, kmer, -1, pDB);
		pGraph->addVertex(contigIter->first, data);
	}
	
	// Generate a k-mer -> contig lookup table for all the contig ends
	std::map<PackedSeq, ContigID> contigLUT;
	for(ContigMap::const_iterator contigIter = contigMap.begin(); contigIter != contigMap.end(); ++contigIter)
	{
		const Sequence& contigSequence = contigIter->second.seq;
		const unsigned numEnds = 2;
		PackedSeq seqs[numEnds];
		seqs[0] = PackedSeq(contigSequence.substr(contigSequence.length() - kmer, kmer)); //SENSE
		seqs[1] = PackedSeq(contigSequence.substr(0, kmer)); // ANTISENSE
		
		size_t numToAdd = (seqs[0] != seqs[1]) ? 2 : 1;
		
		for(unsigned idx = 0; idx < numToAdd; idx++)
		{	
			// insert sequences into the table
			contigLUT[seqs[idx]] = contigIter->first;
		}
	}
	
	// Build the edges
	for(ContigMap::const_iterator contigIter = contigMap.begin(); contigIter != contigMap.end(); ++contigIter)
	{
		// Generate edges to/from this node
		
		// Since two contigs are not necessarily built from the same strand, two contigs can both have OUT nodes pointing to each other
		// this situation will get cleaned up when the links are resolved/merged
		const Sequence& contigSequence = contigIter->second.seq;
		const unsigned numEnds = 2;
		PackedSeq seqs[numEnds];
		seqs[0] = PackedSeq(contigSequence.substr(contigSequence.length() - kmer, kmer)); //SENSE
		seqs[1] = PackedSeq(contigSequence.substr(0, kmer)); // ANTISENSE

		ExtensionRecord extRec;
		int multiplicity;
				
		for(unsigned idx = 0; idx < numEnds; idx++)
		{
			PackedSeq& currSeq = seqs[idx];
			extDirection dir;
			dir = (idx == 0) ? SENSE : ANTISENSE;
						
			pSC->getSeqData(currSeq, extRec, multiplicity);
			
			// Generate the links
			PSequenceVector extensions;
			AssemblyAlgorithms::generateSequencesFromExtension(currSeq, dir, extRec.dir[dir], extensions);
			
			for(PSequenceVector::iterator iter = extensions.begin(); iter != extensions.end(); ++iter)
			{
				// Get the contig this sequence maps to
				bool foundEdge = false;
				for(size_t compIdx = 0; compIdx <= 1; ++compIdx)
				{
					bool reverse = (compIdx == 1);
					PackedSeq testSeq;
					if(reverse)
					{
						testSeq = reverseComplement(*iter);
					}
					else
					{
						testSeq = *iter;
					}
				
					std::map<PackedSeq, ContigID>::iterator cLUTIter;
					cLUTIter = contigLUT.find(testSeq);
					if(cLUTIter != contigLUT.end())
					{						
						pGraph->addEdge(contigIter->first, cLUTIter->second, dir, reverse);
						foundEdge = true;
					}
				}
		
				// it should ALWAYS be found since all sequences in the data set must belong to a contig		
				assert(foundEdge);
			}
		}
	}	
	
	size_t numVert = pGraph->getNumVertices();
	size_t numEdges = pGraph->countEdges(); // SLOW
	printf("Initial graph stats: num vert: %zu num edges: %zu\n", numVert, numEdges);
}
#endif

};
