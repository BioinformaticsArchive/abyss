#include "AlignExtractor.h"
#include "PairUtils.h"
#include "Stats.h"
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

using namespace std;

typedef std::vector<int> IntVector;
struct PairedData
{
	AlignPairVec pairVec[2];
};

struct EstimateReturn
{
	int distance;
	bool isRC;
	unsigned numPairs;
};

typedef std::map<ContigID, PairedData> PairDataMap;

// FUNCTIONS
EstimateReturn estimateDistance(int kmer, int refLen, int pairLen,
		size_t dirIdx, AlignPairVec& pairData, bool sameOrientation,
		const PDF& pdf);
void processContigs(int kmer, std::string alignFile, const ContigLengthVec& lengthVec, const PDF& pdf);

/*
// Go through a list of pairings and provide a maximum likelihood estimate of the distance
int main(int argc, char** argv)
{
	(void)argc;
	std::string distanceCountFile(argv[1]);
	std::string distanceListFile(argv[2]);
	PDF empiricalPDF = loadPDF(distanceCountFile, 350);
	
	std::ifstream inFile(distanceListFile.c_str());
	const int num_samples = atoi(argv[3]);
	
	IntVector distances;
	
	for(int i = 0; i < num_samples; ++i)
	{
		int d;
		inFile >> d;
		if(d < 200)
			distances.push_back(d);
	}
	
	KSTestCont(distances, empiricalPDF);
	
	return 1;
}
*/

int length_cutoff = - 1;
unsigned number_of_pairs_threshold;

int main(int argc, char** argv)
{
	if(argc < 7)
	{
		std::cout << "Usage: <kmer> <SORTED alignFile> <length file> <distance count file> <length cutoff> <num pairs cutoff>\n";
		exit(1);
	}
	
	int kmer = atoi(argv[1]);
	std::string alignFile(argv[2]);
	std::string contigLengthFile(argv[3]);
	std::string distanceCountFile(argv[4]);
	length_cutoff = atoi(argv[5]);
	number_of_pairs_threshold = atoi(argv[6]);

	std::cout << "Alignments: " << alignFile
		<< " Contigs: " << contigLengthFile
		<< " Distribution: " << distanceCountFile
		<< " Length cutoff: " << length_cutoff
		<< " Num pairs cutoff: " << number_of_pairs_threshold
		<< std::endl;

	// Load the pdf
	Histogram distanceHist = loadHist(distanceCountFile);
		
	// Trim off the outliers of the histogram (the bottom 0.01%)
	// These cases result from misalignments
	double trimAmount = 0.0001f;
	Histogram trimmedHist = distanceHist.trim(trimAmount);
	
	//std::cout << "Trimmed hist: \n";
	//trimmedHist.print();
	
	PDF empiricalPDF(trimmedHist);
	
	// Load the length map
	ContigLengthVec contigLens;	
	loadContigLengths(contigLengthFile, contigLens);

	// Estimate the distances between contigs, one at a time
	processContigs(kmer, alignFile, contigLens, empiricalPDF);
	
	return 0;
}

static void assert_open(std::ifstream& f, const std::string& p)
{
	if (f.is_open())
		return;
	std::cerr << p << ": " << strerror(errno) << std::endl;
	exit(EXIT_FAILURE);
}

void processContigs(int kmer, std::string alignFile, const ContigLengthVec& lengthVec, const PDF& pdf)
{
	(void)pdf;
	ifstream in(alignFile.c_str());
	assert_open(in, alignFile);
	AlignExtractor extractor(in);

	// open the output file
	std::ofstream outFile("EstimatedLinks.txt");
	
	int count = 0;
	//Extract the align records from the file, one contig's worth at a time
	bool stop = false;
	
	while(!stop)
	{
		
		AlignPairVec currPairs;
		stop = extractor.extractContigAlignments(currPairs);

		assert(currPairs.size() > 0);
		ContigID refContigID = currPairs.front().refRec.contig;
		
		// From this point all ids will be interpreted as integers
		// They must be strictly > 0 and contiguous
		LinearNumKey refNumericID = convertContigIDToLinearNumKey(refContigID.c_str());
		
		//std::cout << "Ref ctg " << refNumericID << "\n";
		// Only process contigs that are a reasonable length
		int refLength = lookupLength(lengthVec, refNumericID);
		if(refLength < length_cutoff)
		{
			continue;
		}

		outFile << refContigID << " :";

		//std::cout << "Contig " << refContigID << " has " << currPairs.size() << " alignments\n";

		// Seperate the pairings by direction (pairs aligning in the same comp as the contig
		// are sense pairs) and by the contig they align to
		for(size_t dirIdx = 0; dirIdx <= 1; ++dirIdx)
		{
			// If this is the second direction, write a seperator
			if (dirIdx == 1)
				outFile << " |";

			PairDataMap dataMap;
			for(AlignPairVec::iterator iter = currPairs.begin(); iter != currPairs.end(); ++iter)
			{
				if(iter->refRec.isRC == (bool)dirIdx)
				{
					PairedData& pd = dataMap[iter->pairRec.contig];
					size_t compIdx = (size_t)iter->pairRec.isRC;
					assert(compIdx < 2);
					pd.pairVec[compIdx].push_back(*iter);
				}
			}

			// For each contig that is paired, compute the distance
			for(PairDataMap::iterator pdIter = dataMap.begin(); pdIter != dataMap.end(); ++pdIter)
			{
				ContigID pairID = pdIter->first;
				LinearNumKey pairNumID = convertContigIDToLinearNumKey(pairID);
				// get the contig lengths
				int refContigLength = lookupLength(lengthVec, refNumericID);
				int pairContigLength = lookupLength(lengthVec, pairNumID);
				
				// Check if the pairs are in a valid orientation
				if (pdIter->second.pairVec[0].size()
							> number_of_pairs_threshold
						&& pdIter->second.pairVec[1].size()
							> number_of_pairs_threshold) {
					cerr << "warning: inconsistent pairing between "
						<< refContigID << (dirIdx ? '-' : '+') << ' '
						<< pairID << '+' << ' '
						<< pdIter->second.pairVec[1].size()
						<< ' '
						<< pairID << '-' << ' '
						<< pdIter->second.pairVec[0].size()
						<< '\n';
					continue;
				}

				unsigned pairDirIdx = pdIter->second.pairVec[0].size()
					> number_of_pairs_threshold ? 0 : 1;
				AlignPairVec& pairVec
					= pdIter->second.pairVec[pairDirIdx];
				unsigned numPairs = pairVec.size();
				if (numPairs > number_of_pairs_threshold) {
					// Determine the relative orientation of the
					// contigs. As pairs are orientated in opposite
					// (reverse comp) direction, the alignments are in
					// the same orientation if the pairs aligned in
					// the opposite orientation.
					bool sameOrientation = dirIdx != pairDirIdx;

					EstimateReturn er = estimateDistance(kmer,
							refContigLength, pairContigLength,
							dirIdx, pairVec, sameOrientation, pdf);

					if (er.numPairs > number_of_pairs_threshold) {
						Estimate est;
						est.nID = pairNumID;
						est.distance = er.distance;
						est.numPairs = er.numPairs;
						est.stdDev = pdf.getSampleStdDev(er.numPairs);
						est.isRC = er.isRC;
						outFile << ' ' << est;
					} else {
						cerr << "warning: "
							<< refContigID << (dirIdx ? '-' : '+')
							<< ','
							<< pairID << (er.isRC ? '-' : '+') << ' '
							<< er.numPairs << " of "
							<< numPairs << " pairs"
							" fit the expected distribution\n";
					}
				}
			}
		}
		outFile << "\n";
		count++;
		if(count % 10000 == 0)
		{
			std::cout << "Processed " << count << " contigs\n";
		}
	}

	in.close();
	outFile.close();
}

// Estimate the distances between the contigs
EstimateReturn estimateDistance(int kmer, int refLen, int pairLen, size_t dirIdx, AlignPairVec& pairVec, bool sameOrientation,
		const PDF& pdf)
{
	// The provisional fragment sizes are calculated as if the contigs
	// were perfectly adjacent with no overlap or gap.
	IntVector distanceList;
	for (AlignPairVec::iterator apIter = pairVec.begin();
			apIter != pairVec.end(); ++apIter) {
		if (!sameOrientation)
			apIter->pairRec.flipTarget(pairLen);
		int distance;
		if (dirIdx == 0) {
			// refContig is on the left, offset pairContig by the
			// length of refContig
			int refPos = apIter->refRec.targetAtQueryStart();
			int pairPos = refLen
				+ apIter->pairRec.targetAtQueryEnd();
			distance = pairPos - refPos;
		} else {
			// pairContig is on the left, offset refContig by the
			// length of pairContig
			int pairPos = apIter->pairRec.targetAtQueryStart();
			int refPos = pairLen
				+ apIter->refRec.targetAtQueryEnd();
			distance = refPos - pairPos;
		}
		assert(distance > 0);
		distanceList.push_back(distance);
	}
	
	// Perform the max-likelihood est
	unsigned numPairs;
	int dist = maxLikelihoodEst(-kmer+1, pdf.getMaxIdx(), distanceList, pdf, numPairs);
	
	EstimateReturn ret;
	ret.isRC = !sameOrientation;
	ret.distance = dist;
	ret.numPairs = numPairs;
	return ret;
}
