#ifndef PAIRUTILS_H
#define PAIRUTILS_H 1

#include "DirectedGraph.h"
#include "Stats.h" // for Histogram
#include <cmath> // for ceilf
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

struct SimpleContigData
{
	int length;
};

typedef std::vector<int> ContigLengthVec;
typedef DirectedGraph<SimpleContigData> SimpleContigGraph;

// STRUCTURES
struct Estimate
{
	LinearNumKey nID;
	int distance;
	unsigned numPairs;
	float stdDev;
	bool isRC;
	
	friend std::ostream& operator<<(std::ostream& out, const Estimate& object)
	{
		out << object.nID << ","
			<< object.distance
			<< "," << object.numPairs
			<< ","
			<< std::fixed << std::setprecision(1) << object.stdDev
			<< "," << object.isRC;
		return out;
	} 
  
	friend std::istream& operator>> (std::istream& in, Estimate& object)
	{
		// Read 1 record from the stream
		std::string record;
		in >> record;
		
		// parse the record
		std::stringstream recss(record);
		std::stringstream convertor;
		std::string data;
	
		getline(recss, data, ',');
		convertor.str(data);
		convertor >> object.nID;
	
		getline(recss, data, ',');
		convertor.clear();
		convertor.str(data);	
		convertor >> object.distance;
		
		getline(recss, data, ',');
		convertor.clear();
		convertor.str(data);	
		convertor >> object.numPairs;
		
		getline(recss, data, ',');
		convertor.clear();
		convertor.str(data);	
		convertor >> object.stdDev;	

		getline(recss, data, ',');
		convertor.clear();
		convertor.str(data);	
		convertor >> object.isRC;	
		
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

	friend std::ostream& operator<<(std::ostream& out, const SimpleEdgeDesc& object)
	{
		out << object.contig << "," << object.isRC;
		return out;
	} 
  
	friend std::istream& operator>>(std::istream& in, SimpleEdgeDesc& object)
	{
		// Read 1 record from the stream
		std::string record;
		in >> record;
		
		// parse the record
		std::stringstream recss(record);
		std::stringstream convertor;
		std::string data;
	
		getline(recss, data, ',');
		convertor.str(data);
		convertor >> object.contig;
	
		getline(recss, data, ',');
		convertor.clear();
		convertor.str(data);	
		convertor >> object.isRC;
		return in;
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

// Adjacency file
void loadGraphFromAdjFile(SimpleContigGraph* pGraph,  std::string& lengthFile, std::string adjFile);
void parseAdjacencyLine(std::string& adjLine, LinearNumKey currVert, SimpleContigGraph* pGraph);

// Length files
void loadContigLengths(std::string contigLenFile, ContigLengthVec& lengthMap);

// Hist loader
Histogram loadHist(std::string distCountFile);

// Convertor
LinearNumKey convertContigIDToLinearNumKey(const ContigID& id);



#endif
