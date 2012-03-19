#ifndef CONTIGGRAPH_H
#define CONTIGGRAPH_H 1

#include "Graph.h"
#include <utility>

/** A contig graph is a directed graph with the property that
 * the edge (u,v) implies the existence of the edge (~v,~u).
 */
template <typename DG>
class ContigGraph : public DG {
  public:
	// Vertex types.
	typedef typename graph_traits<DG>::vertices_size_type vertices_size_type;
	typedef typename graph_traits<DG>::vertex_descriptor vertex_descriptor;
	typedef typename graph_traits<DG>::vertex_iterator vertex_iterator;
	typedef typename vertex_property<DG>::type vertex_property_type;

	// Edge types.
	typedef typename graph_traits<DG>::degree_size_type degree_size_type;
	typedef typename graph_traits<DG>::edge_descriptor edge_descriptor;
	typedef typename graph_traits<DG>::adjacency_iterator adjacency_iterator;

  public:
	/** Construct an empty contig graph. */
	ContigGraph() { }

	/** Construct a contig graph with n vertices. The underlying
	 * directed graph has two vertices for each contig. */
	ContigGraph(vertices_size_type n) : DG(2 * n) { }

	/** Return the in degree of vertex v. */
	degree_size_type in_degree(vertex_descriptor v) const
	{
		return DG::out_degree(~v);
	}

	/** Remove all out edges from vertex u. */
	void clear_out_edges(vertex_descriptor u)
	{
		std::pair<adjacency_iterator, adjacency_iterator>
			adj = DG::adjacent_vertices(u);
		for (adjacency_iterator v = adj.first; v != adj.second; ++v)
			DG::remove_edge(~*v, ~u);
		DG::clear_out_edges(u);
	}

	/** Remove all in edges from vertex v. */
	void clear_in_edges(vertex_descriptor v)
	{
		clear_out_edges(~v);
	}

	/** Remove all edges to and from vertex v. */
	void clear_vertex(vertex_descriptor v)
	{
		clear_out_edges(v);
		clear_in_edges(v);
	}

	/** Add a vertex to this graph. */
	vertex_descriptor add_vertex(
			const vertex_property_type& data = vertex_property_type())
	{
		vertex_descriptor v = DG::add_vertex(data);
		DG::add_vertex(data);
		return v;
	}

	/** Remove vertex v from this graph. It is assumed that there
	 * are no edges to or from vertex v. It is best to call
	 * clear_vertex before remove_vertex.
	 */
	void remove_vertex(vertex_descriptor v)
	{
		DG::remove_vertex(v);
		DG::remove_vertex(~v);
	}

	/** Add edge (u,v) to this graph. */
	std::pair<edge_descriptor, bool>
	add_edge(vertex_descriptor u, vertex_descriptor v)
	{
		std::pair<edge_descriptor, bool> e = DG::add_edge(u, v);
		DG::add_edge(~v, ~u);
		return e;
	}

  private:
	ContigGraph(const ContigGraph&);
};

#include "AdjIO.h"

template <typename Graph>
std::ostream& operator<<(std::ostream& out,
		const ContigGraph<Graph>& g)
{
	return write_adj(out, g);
}

template <typename Graph>
std::istream& operator>>(std::istream& in,
		ContigGraph<Graph>& g)
{
	return read_adj(in, g);
}

#endif
