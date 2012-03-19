#ifndef PAIRUTILS_H
#define PAIRUTILS_H 1

#include "Stats.h" // for Histogram
#include <cmath> // for ceilf
#include <iomanip>
#include <iostream>
#include <vector>

typedef std::vector<int> ContigLengthVec;

typedef uint32_t LinearNumKey;

/** Distance estimate between two contigs. */
struct Estimate
{
	LinearNumKey nID;
	int distance;
	unsigned numPairs;
	float stdDev;
	bool isRC;
	
	friend std::ostream& operator<<(std::ostream& out,
			const Estimate& o)
	{
		return out << o.nID << ","
			<< o.distance << ","
			<< o.numPairs << ","
			<< std::fixed << std::setprecision(1) << o.stdDev << ","
			<< o.isRC;
	}

	friend std::istream& operator>> (std::istream& in,
			Estimate& o)
	{
		char commas[5] = {};
		in >> o.nID >> commas[0]
			>> o.distance >> commas[1]
			>> o.numPairs >> commas[2]
			>> o.stdDev >> commas[3]
			>> o.isRC;
		assert(std::string(commas) == ",,,,");
		return in;
	}
};

/** Return the allowed error for the given estimate. */
static inline unsigned allowedError(float stddev)
{
	/** The number of standard deviations. */
	const int NUM_SIGMA = 3;

	/**
	 * Additional constant error. The error expected that does not
	 * vary with the number of samples.
	 */
	const unsigned CONSTANT_ERROR = 6;

	return (unsigned)ceilf(NUM_SIGMA * stddev + CONSTANT_ERROR);
}


typedef std::string ContigID;

struct SimpleEdgeDesc
{
	ContigID contig;
	bool isRC;

	SimpleEdgeDesc() { }
	SimpleEdgeDesc(ContigID contig, bool isRC)
		: contig(contig), isRC(isRC) { }

	friend std::ostream& operator<<(std::ostream& out,
			const SimpleEdgeDesc& o)
	{
		return out << o.contig << "," << o.isRC;
	}

	friend std::istream& operator>>(std::istream& in,
			SimpleEdgeDesc& o)
	{
		getline(in, o.contig, ',');
		return in >> o.isRC;
	}
};

typedef std::vector<Estimate> EstimateVector;

struct EstimateRecord;

std::istream& readEstimateRecord(std::istream& stream,
		EstimateRecord& er);

struct EstimateRecord
{
	LinearNumKey refID;
	EstimateVector estimates[2];
	friend std::istream& operator >>(std::istream& in,
			EstimateRecord& er)
	{
		return readEstimateRecord(in, er);
	}
};

void loadContigLengths(const std::string& path,
		ContigLengthVec& lengths);

LinearNumKey convertContigIDToLinearNumKey(const ContigID& id);

#endif
