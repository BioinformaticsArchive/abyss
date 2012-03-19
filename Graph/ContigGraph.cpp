#include "ContigGraph.h"
#include "ContigID.h"
#include "DirectedGraphImpl.h"
#include <cassert>
#include <fstream>
#include <limits> // for numeric_limits
#include <sstream>
#include <string>

using namespace std;

namespace opt {
	/** Abort the search after visiting maxCost vertices. */
	unsigned maxCost = 100000;
};

/** The length of each contig. */
static vector<unsigned> g_contigLengths;

/** Return the length of this contig in k-mer. */
unsigned ContigNode::length() const
{
	assert(!ambiguous());
	return g_contigLengths[id()];
}

// Explicit instantiation.
template class DirectedGraph<SimpleContigData>;

static void readEdges(istream& in, LinearNumKey id,
		SimpleContigGraph& graph)
{
	for (int sense = false; sense <= true; ++sense) {
		string s;
		getline(in, s, !sense ? ';' : '\n');
		assert(in.good());
		istringstream ss(s);
		for (ContigNode edge; ss >> edge;)
			graph.add_edge(ContigNode(id, sense),
					sense ? ~edge : edge);
		assert(ss.eof());
	}
}

/** Read an adjacency graph. */
istream& operator>>(istream& in, SimpleContigGraph& o)
{
	assert(g_contigIDs.empty());
	assert(g_contigLengths.empty());

	// Load the vertices.
	string id;
	unsigned length;
	while (in >> id >> length) {
		in.ignore(numeric_limits<streamsize>::max(), '\n');
		(void)stringToID(id);
		assert(length >= opt::k);
		g_contigLengths.push_back(length - opt::k + 1);
	}
	assert(in.eof());
	g_contigIDs.lock();

	o.clear();
	SimpleContigGraph(2 * g_contigLengths.size()).swap(o);

	// Load the edges.
	in.clear();
	in.seekg(ios_base::beg);
	assert(in);
	while (in >> id) {
		in.ignore(numeric_limits<streamsize>::max(), ';');
		readEdges(in, stringToID(id), o);
	}
	assert(in.eof());
	return in;
}

/** Read an adjacency graph. */
void loadGraphFromAdjFile(SimpleContigGraph* pGraph,
		const string& adjFile)
{
	ifstream in(adjFile.c_str());
	assert(in.is_open());
	in >> *pGraph;
	assert(in.eof());
}
