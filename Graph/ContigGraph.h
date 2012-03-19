#ifndef CONTIGGRAPH_H
#define CONTIGGRAPH_H 1

#include "DirectedGraph.h"
#include <cassert>
#include <istream>
#include <limits> // for numeric_limits
#include <ostream>
#include <sstream>
#include <utility>

template <typename VertexProp = no_property> class ContigGraph;

template <typename VertexProp>
std::ostream& operator<<(std::ostream& out,
		const ContigGraph<VertexProp>& g);

/** A contig graph is a directed graph with the property that
 * the edge (u,v) implies the existence of the edge (~v,~u).
 */
template <typename VertexProp>
class ContigGraph : public DirectedGraph<VertexProp> {
  public:
	typedef DirectedGraph<VertexProp> DG;

	// Vertex types.
	typedef typename DG::vertices_size_type vertices_size_type;
	typedef typename DG::vertex_descriptor vertex_descriptor;
	typedef typename DG::vertex_iterator vertex_iterator;
	typedef typename DG::Vertex Vertex;

	// Edge types.
	typedef typename DG::degree_size_type degree_size_type;
	typedef typename DG::out_edge_iterator out_edge_iterator;

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

	/** Remove all out edges from vertex v. */
	void clear_out_edges(vertex_descriptor v)
	{
		const Vertex& vertex = (*this)[v];
		for (out_edge_iterator it = vertex.begin();
				it != vertex.end(); ++it)
			remove_edge(~target(*it), ~v);
		DG::clear_out_edges(v);
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
				const VertexProp& data = VertexProp())
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
	void add_edge(vertex_descriptor u, vertex_descriptor v)
	{
		DG::add_edge(u, v);
		DG::add_edge(~v, ~u);
	}

	friend std::ostream& operator<< <>(std::ostream& out,
			const ContigGraph& g);

	// The following functions are not standard.

	/** Return whether the outgoing edge of the specified vertex is
	 * contiguous. */
	bool contiguous_out(vertex_descriptor v) const
	{
		return out_degree(v) == 1
			&& in_degree((*this)[v].front().target()) == 1;
	}

	/** Return whether the incoming edge of the specified vertex is
	 * contiguous. */
	bool contiguous_in(vertex_descriptor v) const
	{
		return contiguous_out(~v);
	}

	/** @todo *it should be a vertex_descriptor not a Vertex */
	bool contiguous_out(const Vertex& v) const
	{
		return contiguous_out(vertex(v));
	}

	/** @todo *it should be a vertex_descriptor not a Vertex */
	bool contiguous_in(const Vertex& v) const
	{
		return contiguous_in(vertex(v));
	}

	/** Copy the outgoing edges of vertex u to vertex v. */
	void copy_out_edges(vertex_descriptor u, vertex_descriptor v)
	{
		assert(u != v);
		std::pair<out_edge_iterator, out_edge_iterator>
			edges = out_edges(u);
		for (out_edge_iterator it = edges.first;
				it != edges.second; ++it)
			add_edge(v, target(*it));
	}

	/** Add the incoming edges of vertex u to vertex v. */
	void copy_in_edges(vertex_descriptor u, vertex_descriptor v)
	{
		copy_out_edges(~u, ~v);
	}

  private:
	ContigGraph(const ContigGraph&);
};

/** Output a contig adjacency graph. */
template <typename VertexProp>
std::ostream& operator<<(std::ostream& out,
		const ContigGraph<VertexProp>& g)
{
	typedef ContigGraph<VertexProp> G;
	for (typename G::vertex_iterator v = g.begin();
			v != g.end(); ++v) {
		const ContigNode& id = g.vertex(*v);
		if (g.is_removed(id))
			continue;
		if (!id.sense())
			out << ContigID(id) << *v;
		out << "\t;";
		for (typename G::out_edge_iterator e = v->begin();
				e != v->end(); ++e)
			out << ' ' << (g.target(*e) ^ id.sense());
		if (id.sense())
			out << '\n';
	}
	return out;
}

/** Read a contig adjacency graph. */
template <typename VertexProp>
std::istream& operator>>(std::istream& in, ContigGraph<VertexProp>& g)
{
	typedef typename ContigGraph<VertexProp>::DG DG;
	typedef typename
		ContigGraph<VertexProp>::vertex_descriptor vertex_descriptor;

	// Read the vertex properties.
	g.clear();
	assert(in);
	ContigID id(-1);
	VertexProp prop;
	while (in >> id >> prop) {
		in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
		vertex_descriptor v = g.add_vertex(prop);
		assert(v == ContigNode(id, false));
	}
	assert(in.eof());
	assert(g.num_vertices() > 0);
	ContigID::lock();

	// Read the edges.
	in.clear();
	in.seekg(std::ios_base::beg);
	assert(in);
	while (in >> id) {
		in.ignore(std::numeric_limits<std::streamsize>::max(), ';');
		for (int sense = false; sense <= true; ++sense) {
			std::string s;
			getline(in, s, !sense ? ';' : '\n');
			assert(in.good());
			std::istringstream ss(s);
			for (ContigNode edge; ss >> edge;)
				g.DG::add_edge(ContigNode(id, sense), edge ^ sense);
			assert(ss.eof());
		}
	}
	assert(in.eof());
	return in;
}

#endif
