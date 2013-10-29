#ifndef BIDIRECTIONALBFS_H
#define BIDIRECTIONALBFS_H 1

#include "Graph/DefaultColorMap.h"
#include "Graph/BidirectionalBFSVisitor.h"
#include "Graph/Path.h"
#include <vector>
#include <boost/graph/breadth_first_search.hpp>

using boost::function_requires;
using boost::graph_traits;
using boost::property_traits;
using boost::color_traits;

template <class BidirectionalGraph, class Buffer, class ColorMap>
inline BFSVisitorResult
bidirectionalBFS_visit_edge(
	const BidirectionalGraph& g,
	typename boost::graph_traits<BidirectionalGraph>::edge_descriptor e,
	Buffer& Q,
	BidirectionalBFSVisitor<BidirectionalGraph>& vis,
	ColorMap& color1,
	ColorMap& color2,
	Direction dir)
{
	function_requires< boost::BidirectionalGraphConcept<BidirectionalGraph> >();
	typedef graph_traits<BidirectionalGraph> GTraits;
	typedef typename GTraits::vertex_descriptor Vertex;
	typedef typename GTraits::edge_descriptor Edge;
	function_requires< boost::ReadWritePropertyMapConcept<ColorMap, Vertex> >();
	typedef typename property_traits<ColorMap>::value_type ColorValue;
	typedef color_traits<ColorValue> Color;

	ColorMap& color = (dir == FORWARD) ? color1 : color2;
	ColorMap& other_color = (dir == FORWARD) ? color2 : color1;

	Vertex v = (dir == FORWARD) ? target(e, g) : source(e, g);
	vis.examine_edge(e, g, dir);

	ColorValue v_color = get(color, v);
	ColorValue other_v_color = get(other_color, v);

	BFSVisitorResult result;

	if (other_v_color != Color::white()) {
		// Tricky point: Each common edge is visited
		// by both the forward and reverse traversal.
		// To avoid redundant visitor events, we only
		// report a common edge when we encounter it
		// in the forward direction.
		if (dir == FORWARD) {
			// A return value of SKIP_ELEMENT is silently
			// ignored here because it has no logical consequence
			if (vis.common_edge(e, g) == ABORT_SEARCH)
				return ABORT_SEARCH;
		}
		put(color, v, Color::black());
	}
	else if (v_color == Color::white()) {
		result = vis.tree_edge(e, g, dir);
		if (result == SKIP_ELEMENT || result == ABORT_SEARCH)
			return result;
		put(color, v, Color::gray());
		vis.discover_vertex(v, g, dir);
		Q.push(v);
	}
	else {
		result = vis.non_tree_edge(e, g, dir);
		if (result == SKIP_ELEMENT || result == ABORT_SEARCH)
			return result;
		if (v_color == Color::gray())
			vis.gray_target(e, g, dir);
		else
			vis.black_target(e, g, dir);
	}

	return SUCCESS;
}

template <class BidirectionalGraph, class Buffer, class ColorMap>
void bidirectionalBFS(
	const BidirectionalGraph& g,
	typename boost::graph_traits<BidirectionalGraph>::vertex_descriptor s1,
	typename boost::graph_traits<BidirectionalGraph>::vertex_descriptor s2,
	Buffer& Q1,
	Buffer& Q2,
	BidirectionalBFSVisitor<BidirectionalGraph>& vis,
	ColorMap& color1,
	ColorMap& color2)
{
	function_requires< boost::BidirectionalGraphConcept<BidirectionalGraph> >();
	typedef graph_traits<BidirectionalGraph> GTraits;
	typedef typename GTraits::vertex_descriptor Vertex;
	typedef typename GTraits::edge_descriptor Edge;
	function_requires< boost::ReadWritePropertyMapConcept<ColorMap, Vertex> >();
	typedef typename property_traits<ColorMap>::value_type ColorValue;
	typedef color_traits<ColorValue> Color;

	typename GTraits::out_edge_iterator oei, oei_end;
	typename GTraits::in_edge_iterator iei, iei_end;

	put(color1, s1, Color::gray());
	put(color2, s2, Color::gray());
	vis.discover_vertex(s1, g, FORWARD);
	vis.discover_vertex(s2, g, REVERSE);
	Q1.push(s1);
	Q2.push(s2);

	Direction dir = FORWARD;
	while (!Q1.empty() || !Q2.empty()) {

		Buffer& Q = (dir == FORWARD) ? Q1 : Q2;
		ColorMap& color = (dir == FORWARD) ? color1 : color2;

		Vertex u = Q.top(); Q.pop();
		vis.examine_vertex(u, g, dir);

		if (dir == FORWARD) {
			for (boost::tie(oei, oei_end) = out_edges(u, g); oei != oei_end; ++oei) {
				if (bidirectionalBFS_visit_edge(g, *oei, Q, vis,
					color1, color2, dir) == ABORT_SEARCH) {
					return;
				}
			}
		} else {
			for (boost::tie(iei, iei_end) = in_edges(u, g); iei != iei_end; ++iei) {
				if (bidirectionalBFS_visit_edge(g, *iei, Q, vis,
					color1, color2, dir) == ABORT_SEARCH) {
					return;
				}
			}
		}

		put(color, u, Color::black());
		vis.finish_vertex(u, g, dir);

		if (dir == REVERSE && !Q1.empty())
			dir = FORWARD;
		else if (dir == FORWARD && !Q2.empty())
			dir = REVERSE;

	} // while(!Q1.empty() || !Q2.empty())

} // bidirectionalBFS

template <class BidirectionalGraph>
void bidirectionalBFS(const BidirectionalGraph& g,
	typename graph_traits<BidirectionalGraph>::vertex_descriptor start,
	typename graph_traits<BidirectionalGraph>::vertex_descriptor goal,
	BidirectionalBFSVisitor<BidirectionalGraph>& visitor)
{
	typedef typename graph_traits<BidirectionalGraph>::vertex_descriptor V;
	DefaultColorMap<BidirectionalGraph> colorMap1;
	DefaultColorMap<BidirectionalGraph> colorMap2;
	boost::queue<V> q1;
	boost::queue<V> q2;
	bidirectionalBFS(g, start, goal, q1, q2, visitor, colorMap1, colorMap2);
}

#endif
