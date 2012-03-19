#ifndef GRAPH_H
#define GRAPH_H 1

#include "config.h"
#if HAVE_BOOST_GRAPH_GRAPH_TRAITS_HPP
# include <boost/graph/graph_traits.hpp>
#endif
#include <utility> // for pair

// Graph

template <typename G>
struct graph_traits {
	// Graph
	typedef typename G::vertex_descriptor vertex_descriptor;
	typedef typename G::directed_category directed_category;
	typedef typename G::traversal_category traversal_category;
	typedef typename G::edge_parallel_category edge_parallel_category;

	// IncidenceGraph
	typedef typename G::edge_descriptor edge_descriptor;
	typedef typename G::out_edge_iterator out_edge_iterator;
	typedef typename G::degree_size_type degree_size_type;

	// BidirectionalGraph
	typedef typename G::in_edge_iterator in_edge_iterator;

	// AdjacencyGraph
	typedef typename G::adjacency_iterator adjacency_iterator;

	// VertexListGraph
	typedef typename G::vertex_iterator vertex_iterator;
	typedef typename G::vertices_size_type vertices_size_type;

	// EdgeListGraph
	typedef typename G::edge_iterator edge_iterator;
	typedef typename G::edges_size_type edges_size_type;
};

#if !HAVE_BOOST_GRAPH_GRAPH_TRAITS_HPP

// IncidenceGraph

template <class G>
typename graph_traits<G>::vertex_descriptor
source(std::pair<typename graph_traits<G>::vertex_descriptor,
		typename graph_traits<G>::vertex_descriptor> e, const G&)
{
	return e.first;
}

template <class G>
typename graph_traits<G>::vertex_descriptor
target(std::pair<typename graph_traits<G>::vertex_descriptor,
		typename graph_traits<G>::vertex_descriptor> e, const G&)
{
	return e.second;
}

// PropertyGraph

template<typename Graph, typename Descriptor,
	typename Bundle, typename T>
T get(T Bundle::* tag, const Graph& g, Descriptor x)
{
	return g[x].*tag;
}

#endif // HAVE_BOOST_GRAPH_GRAPH_TRAITS_HPP

// VertexMutablePropertyGraph

template <typename Graph>
class vertex_property {
  public:
	typedef typename Graph::vertex_property_type type;
};

// EdgeMutablePropertyGraph

template <typename Graph>
class edge_property {
  public:
	typedef typename Graph::edge_property_type type;
};

#endif
