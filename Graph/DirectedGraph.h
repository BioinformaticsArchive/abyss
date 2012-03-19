#ifndef DIRECTEDGRAPH_H
#define DIRECTEDGRAPH_H 1

#include "AffixIterator.h"
#include "ContigNode.h"
#include <cassert>
#include <iterator>
#include <ostream>
#include <vector>

/** A directed graph. */
template<typename D>
class DirectedGraph
{
  public:
	class Edge;
	typedef typename std::vector<Edge> Edges;

/** A vertex and its properties. */
class Vertex
{
  public:
	Vertex() { }
	Vertex(const D& d) : m_data(d) { }

	/** Return a collection of outgoing edges. */
	const Edges& out_edges() const { return m_edges; }

	/** Return the number of outgoing edges. */
	size_t out_degree() const
	{
		return m_edges.size();
	}

	/** Add an edge to this vertex. */
	void add_edge(Vertex* v)
	{
		for (typename Edges::const_iterator it = m_edges.begin();
				it != m_edges.end(); ++it)
			assert(v != &it->target());
		m_edges.push_back(v);
	}

	bool operator ==(const Vertex& v) const { return this == &v; }
	bool operator !=(const Vertex& v) const { return this != &v; }

	friend std::ostream& operator <<(std::ostream& out,
			const Vertex& o)
	{
		if (o.m_edges.empty())
			return out;
		out << '"' << &o << "\" ->";
		if (o.m_edges.size() > 1)
			out << " {";
		std::copy(o.m_edges.begin(), o.m_edges.end(),
				affix_ostream_iterator<Edge>(out, " \"", "\""));
		if (o.m_edges.size() > 1)
			out << " }";
		return out;
	}

  private:
	D m_data;
	Edges m_edges;
};

/** A directed edge. */
class Edge
{
  public:
	Edge(Vertex* v) : m_target(v) { }

	/** Returns the target vertex of this edge. */
	const Vertex& target() const { return *m_target; }

	friend std::ostream& operator <<(std::ostream& out,
			const Edge& e)
	{
		return out << e.m_target;
	}

  private:
	/** The target vertex of this edge. */
	Vertex* m_target;
};

	public:
		typedef unsigned vertices_size_type;
		typedef unsigned degree_size_type;
		typedef ContigNode Node;
		typedef const Node& vertex_descriptor;
		typedef typename std::vector<Vertex> Vertices;
		typedef typename Vertices::const_iterator const_iterator;

		/** Create an empty graph. */
		DirectedGraph() { }

		/** Create a graph with n vertices and zero edges. */
		DirectedGraph(vertices_size_type n) : m_vertices(n) { }

		/** Swap this graph with graph x. */
		void swap(DirectedGraph& x) { m_vertices.swap(x.m_vertices); }

		/** Return the vertex specified by the given descriptor. */
		const Vertex& operator[](vertex_descriptor v) const
		{
			return m_vertices[v.index()];
		}

		/** Return the vertex specified by the given descriptor. */
		Vertex& operator[](vertex_descriptor v)
		{
			return m_vertices[v.index()];
		}

		/** Remove all the edges and vertices from this graph. */
		void clear() { m_vertices.clear(); }

		/** Adds vertex v to the graph. */
		void add_vertex(vertex_descriptor v, const D& data = D())
		{
			assert(m_vertices.size() == v.index());
			m_vertices.push_back(Vertex(data));
		}

		/** Adds edge (u,v) to the graph. */
		void add_edge(vertex_descriptor u, vertex_descriptor v)
		{
			assert(u.index() < m_vertices.size());
			assert(v.index() < m_vertices.size());
			(*this)[u].add_edge(&(*this)[v]);
		}

		/** Return the number of vertices. */
		size_t num_vertices() const { return m_vertices.size(); }

		/** Return an iterator to the vertex set of this graph. */
		const_iterator begin() const { return m_vertices.begin(); }
		const_iterator end() const { return m_vertices.end(); }

		/** Return the number of edges. */
		size_t num_edges() const
		{
			size_t n = 0;
			for (typename Vertices::const_iterator it
					= m_vertices.begin(); it != m_vertices.end(); ++it)
				n += it->out_degree();
			return n;
		}

		/** Return the out degree of the specified vertex. */
		degree_size_type out_degree(vertex_descriptor v) const
		{
			return (*this)[v].out_degree();
		}

		/** Return the in degree of the specified vertex. */
		degree_size_type in_degree(vertex_descriptor v) const
		{
			return (*this)[~v].out_degree();
		}

		/** Return the in degree of the specified vertex. */
		degree_size_type in_degree(const Vertex& v) const
		{
			return in_degree(vertex(v));
		}

		/** Return the nth vertex. */
		static Node vertex(vertices_size_type n)
		{
			return Node(n);
		}

		/** Return the descriptor of the specified vertex. */
		Node vertex(const Vertex& v) const
		{
			assert(&m_vertices[0] <= &v
					&& &v <= &m_vertices[0] + m_vertices.size());
			return vertex(&v - &m_vertices[0]);
		}

		/** Return the target vertex of the specified edge. */
		Node target(const Edge& e) const
		{
			return vertex(e.target());
		}

		friend std::ostream& operator <<(std::ostream& out,
				const DirectedGraph<D>& g)
		{
			for (const_iterator v = g.begin(); v != g.end(); ++v) {
				if (v->out_degree() == 0)
					continue;
				out << '"' << g.vertex(*v) << "\" ->";
				if (v->out_degree() > 1)
					out << " {";
				for (typename Edges::const_iterator e
						= v->out_edges().begin();
						e != v->out_edges().end(); ++e)
					out << " \"" << g.target(*e) << '"';
				if (v->out_degree() > 1)
					out << " }";
				out << '\n';
			}
			return out;
		}

	private:
		DirectedGraph(const DirectedGraph& x);
		DirectedGraph& operator =(const DirectedGraph& x);

		Vertices m_vertices;
};

#endif
