#ifndef DOTIO_H
#define DOTIO_H 1

#include "Graph.h"
#include <ostream>

template <typename V, typename VertexProp>
void write_vertex(std::ostream& out,
		V u, const VertexProp& vp)
{
	out << '"' << u << "\" [" << vp << "]\n";
}

template <typename V>
void write_vertex(std::ostream&, V, no_property)
{
}

template <typename Graph, typename EdgeProp>
void write_edges(std::ostream& out, const Graph& g,
		typename graph_traits<Graph>::vertex_descriptor u,
		const EdgeProp*)
{
	typedef typename graph_traits<Graph>::adjacency_iterator
		adjacency_iterator;
	typedef typename graph_traits<Graph>::edge_descriptor
		edge_descriptor;
	typedef typename edge_property<Graph>::type edge_property_type;
	std::pair<adjacency_iterator, adjacency_iterator>
		adj = adjacent_vertices(u, g);
	for (adjacency_iterator v = adj.first; v != adj.second; ++v) {
		assert(!get(vertex_removed, g, *v));
		out << '"' << u << "\" -> \"" << *v << '"';
		const edge_property_type& ep = get(edge_bundle, g,
				edge_descriptor(u, *v));
		if (!(ep == edge_property_type()))
			out << " [" << ep << ']';
		out << '\n';
	}
}

template <typename Graph>
void write_edges(std::ostream& out, const Graph& g,
		typename graph_traits<Graph>::vertex_descriptor u,
		const no_property*)
{
	typedef typename graph_traits<Graph>::adjacency_iterator
		adjacency_iterator;
	unsigned outdeg = out_degree(u, g);
	if (outdeg == 0)
		return;
	out << '"' << u << "\" ->";
	if (outdeg > 1)
		out << " {";
	std::pair<adjacency_iterator, adjacency_iterator>
		adj = adjacent_vertices(u, g);
	for (adjacency_iterator v = adj.first; v != adj.second; ++v)
		out << " \"" << *v << '"';
	if (outdeg > 1)
		out << " }";
	out << '\n';
}

/** Output a GraphViz dot graph. */
template <typename Graph>
std::ostream& write_dot(std::ostream& out, const Graph& g)
{
	typedef typename graph_traits<Graph>::vertex_iterator
		vertex_iterator;
	typedef typename edge_property<Graph>::type edge_property_type;
	std::pair<vertex_iterator, vertex_iterator> vit = vertices(g);
	for (vertex_iterator u = vit.first; u != vit.second; ++u) {
 		if (get(vertex_removed, g, *u))
			continue;
		write_vertex(out, *u, get(vertex_bundle, g, *u));
		write_edges(out, g, *u, (edge_property_type*)NULL);
	}
	return out;
}

/** Output a GraphViz dot graph. */
template <typename Graph>
struct DotWriter
{
	const Graph& g;
	DotWriter(const Graph& g) : g(g) { }
	friend std::ostream& operator<<(std::ostream& out,
			const DotWriter& o)
	{
		return write_dot<Graph>(out, o.g);
	}
};

/** Output a GraphViz dot graph. */
template <typename Graph>
DotWriter<Graph> dot_writer(const Graph& g)
{
	return DotWriter<Graph>(g);
}

#endif
