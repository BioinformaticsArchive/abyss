#ifndef CONTIGGRAPH_H
#define CONTIGGRAPH_H 1

#include "DirectedGraph.h"
#include <istream>

struct NoContigData { };

class ContigGraph;

std::istream& operator>>(std::istream& in, ContigGraph& o);

void readContigGraph(ContigGraph& graph, const std::string& path);

/** A contig graph is a directed graph with the property that
 * the edge (u,v) implies the existence of the edge (~v,~u).
 */
class ContigGraph : public DirectedGraph<NoContigData> {
	typedef DirectedGraph<NoContigData> DG;

  public:
	/** Construct an empty contig graph. */
	ContigGraph() { }

	/** Construct a contig graph with n vertices. The underlying
	 * directed graph has two vertices for each contig. */
	ContigGraph(vertices_size_type n) : DG(2 * n) { }

	/** Return the in degree of vertex v. */
	degree_size_type in_degree(vertex_descriptor v) const
	{
		return (*this)[~v].out_degree();
	}

	/** Return the in degree of vertex v. */
	degree_size_type in_degree(const Vertex& v) const
	{
		return in_degree(vertex(v));
	}

  private:
	ContigGraph(const ContigGraph&);
};

#endif
