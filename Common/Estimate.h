#ifndef ESTIMATE_H
#define ESTIMATE_H 1

#include "ContigID.h"
#include "ContigNode.h"
#include "IOUtil.h"
#include <cassert>
#include <cmath> // for ceilf
#include <iomanip>
#include <istream>
#include <iterator>
#include <ostream>
#include <sstream>
#include <string>

namespace opt {
	extern int dot;

	/** The acceptable error of a distance estimate. */
	extern unsigned distanceError;
}

/** An estimate of the distance between two contigs. */
struct DistanceEst
{
	int distance;
	unsigned numPairs;
	float stdDev;

	bool operator==(const DistanceEst& o) const
	{
		return distance == o.distance
			&& numPairs == o.numPairs
			&& stdDev == o.stdDev;
	}

	friend std::ostream& operator<<(std::ostream& out,
			const DistanceEst& o)
	{
		if (opt::dot)
			return out <<
				"d=" << o.distance << " "
				"e=" << std::fixed << std::setprecision(1)
					<< o.stdDev << " "
				"n=" << o.numPairs;
		else
			return out << o.distance << ',' << o.numPairs << ','
				<< std::fixed << std::setprecision(1) << o.stdDev;
	}

	friend std::istream& operator>>(std::istream& in, DistanceEst& o)
	{
		if (in >> std::ws && in.peek() == 'd')
			return in >> expect("d =") >> o.distance
				>> expect(" e =") >> o.stdDev
				>> expect(" n =") >> o.numPairs;
		else
			return in >> o.distance >> expect(",")
				>> o.numPairs >> expect(",")
				>> o.stdDev;
	}
};

/** An estimate of the distance between two contigs. */
struct Estimate
{
	ContigNode contig;
	int distance;
	unsigned numPairs;
	float stdDev;

	friend std::ostream& operator<<(std::ostream& out,
			const Estimate& o)
	{
		if (opt::dot)
			return out << '"' << o.contig << "\" ["
				"d=" << o.distance << " "
				"e=" << std::fixed << std::setprecision(1)
					<< o.stdDev << " "
				"n=" << o.numPairs << ']';
		else
			return out << o.contig << ','
				<< o.distance << ','
				<< o.numPairs << ','
				<< std::fixed << std::setprecision(1) << o.stdDev;
	}

	friend std::istream& operator>>(std::istream& in, Estimate& o)
	{
		in >> std::ws;
		std::string sID;
		char comma0, comma1;
		getline(in, sID, ',')
			>> o.distance >> comma0
			>> o.numPairs >> comma1
			>> o.stdDev;
		if (in)
			o.contig = ContigNode(sID);
		return in;
	}
};

/** Return the allowed error for the given estimate. */
static inline unsigned allowedError(float stddev)
{
	/** The number of standard deviations. */
	const int NUM_SIGMA = 3;
	return (unsigned)ceilf(NUM_SIGMA * stddev + opt::distanceError);
}

typedef std::vector<Estimate> EstimateVector;

/** Distance estimates to and from a particular contig. */
struct EstimateRecord
{
	ContigID refID;
	EstimateVector estimates[2];

	/** Read the distance estimates for one contig. */
	friend std::istream& operator >>(std::istream& in,
			EstimateRecord& o)
	{
		o.estimates[false].clear();
		o.estimates[true].clear();

		ContigID id;
		in >> id;
		if (!in)
			return in;
		o.refID = id;

		for (int rc = false; rc <= true; ++rc) {
			std::string s;
			std::getline(in, s, !rc ? ';' : '\n');
			std::istringstream ss(s);
			std::copy(std::istream_iterator<Estimate>(ss),
					std::istream_iterator<Estimate>(),
					std::back_inserter(o.estimates[rc]));
			assert(ss.eof());
		}

		return in;
	}

};

namespace std {
	template<>
	inline void swap(EstimateRecord&, EstimateRecord&)
	{
		assert(false);
	}
}

#endif
