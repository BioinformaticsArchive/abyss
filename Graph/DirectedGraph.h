#ifndef DIRECTEDGRAPH_H
#define DIRECTEDGRAPH_H 1

#include "AffixIterator.h"
#include "ContigNode.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <ostream>
#include <utility>
#include <vector>

/** No properties. */
struct no_property
{
	friend std::ostream& operator <<(std::ostream& out,
			const no_property&)
	{
		return out;
	}

	friend std::istream& operator >>(std::istream& in, no_property&)
	{
		return in;
	}
};

/** A directed graph. */
template <typename VertexProp = no_property>
class DirectedGraph
{
	class Vertex;
	typedef typename std::vector<Vertex> Vertices;
	class Edge;
	typedef typename std::vector<Edge> Edges;
  public:
	typedef unsigned vertices_size_type;
	typedef ContigNode vertex_descriptor;
	typedef unsigned edges_size_type;
	typedef unsigned degree_size_type;
	typedef const Edge* edge_descriptor;

	/** Not implemented. */
	typedef void edge_iterator;

/** Iterate through the vertices of this graph. */
class vertex_iterator
	: public std::iterator<std::input_iterator_tag, vertex_descriptor>
{
	typedef typename Vertices::const_iterator const_iterator;

  public:
	vertex_iterator(const const_iterator& it,
			const vertex_descriptor& v) : m_it(it), m_v(v) { }
	const vertex_descriptor& operator *() const { return m_v; }
	const Vertex* operator ->() const { return &*m_it; }

	bool operator ==(const vertex_iterator& it) const
	{
		return m_it != it.m_it;
	}

	bool operator !=(const vertex_iterator& it) const
	{
		return m_it != it.m_it;
	}

	vertex_iterator& operator ++() { ++m_it; ++m_v; return *this; }
	vertex_iterator operator ++(int)
	{
		vertex_iterator it = *this;
		++*this;
		return it;
	}

  private:
	const_iterator m_it;
	vertex_descriptor m_v;
};

/** Iterate through the out-edges. */
class out_edge_iterator
	: public std::iterator<std::input_iterator_tag, edge_descriptor>
{
	typedef typename Edges::const_iterator const_iterator;

  public:
	out_edge_iterator(const const_iterator& it) : m_it(it) { }
	edge_descriptor operator *() const { return &*m_it; }

	bool operator ==(const out_edge_iterator& it) const
	{
		return m_it != it.m_it;
	}

	bool operator !=(const out_edge_iterator& it) const
	{
		return m_it != it.m_it;
	}

	out_edge_iterator& operator ++() { ++m_it; return *this; }
	out_edge_iterator operator ++(int)
	{
		out_edge_iterator it = *this;
		++*this;
		return it;
	}

  private:
	const_iterator m_it;
};

  private:
/** A vertex and its properties. */
class Vertex : public VertexProp
{
  public:
	Vertex() { }
	Vertex(const VertexProp& p) : VertexProp(p) { }

	/** Return the properties of this vertex. */
	const VertexProp& get_property() const { return *this; }

	/** Return an iterator to the edges of this vertex. */
	out_edge_iterator begin() const { return m_edges.begin(); }
	out_edge_iterator end() const { return m_edges.end(); }

	/** Return the first out edge of this vertex. */
	const Edge& front() const
	{
		assert(!m_edges.empty());
		return m_edges.front();
	}

	/** Return the number of outgoing edges. */
	size_t out_degree() const
	{
		return m_edges.size();
	}

	/** Add an edge to this vertex. */
	std::pair<typename DirectedGraph<VertexProp>::edge_descriptor,
		bool>
	add_edge(vertex_descriptor v)
	{
		assert(count(m_edges.begin(), m_edges.end(), v) == 0);
		m_edges.push_back(Edge(v));
		return std::make_pair(&m_edges.back(), true);
	}

	/** Remove the edge to v from this vertex. */
	void remove_edge(vertex_descriptor v)
	{
		m_edges.erase(find(m_edges.begin(), m_edges.end(), v));
	}

	/** Remove all out edges from this vertex. */
	void clear_out_edges()
	{
		m_edges.clear();
	}

  private:
	Edges m_edges;
};

/** A directed edge. */
class Edge
{
  public:
	explicit Edge(vertex_descriptor v) : m_target(v) { }

	/** Returns the target vertex of this edge. */
	const vertex_descriptor& target() const { return m_target; }

	/** Return true if the target of this edge is v. */
	bool operator ==(const vertex_descriptor& v) const
	{
		return m_target == v;
	}

  private:
	/** The target vertex of this edge. */
	vertex_descriptor m_target;
};

	public:
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

		/** Return an iterator to the vertex set of this graph. */
		vertex_iterator begin() const
		{
			return vertex_iterator(m_vertices.begin(),
					vertex_descriptor(0));
		}

		vertex_iterator end() const
		{
			return vertex_iterator(m_vertices.end(),
					vertex_descriptor(m_vertices.size()));
		}

		/** Returns an iterator-range to the vertices. */
		std::pair<vertex_iterator, vertex_iterator> vertices() const
		{
			return make_pair(begin(), end());
		}

		/** Remove all the edges and vertices from this graph. */
		void clear() { m_vertices.clear(); }

		/** Add vertex v to the graph. */
		vertex_descriptor add_vertex(
				const VertexProp& data = VertexProp())
		{
			m_vertices.push_back(Vertex(data));
			return vertex_descriptor(m_vertices.size() - 1);
		}

		/** Returns an iterator-range to the out edges of vertex u. */
		std::pair<out_edge_iterator, out_edge_iterator>
		out_edges(vertex_descriptor u) const
		{
			assert(u.index() < m_vertices.size());
			const Vertex& v = (*this)[u];
			return make_pair(v.begin(), v.end());
		}

		/** Adds edge (u,v) to the graph. */
		std::pair<typename DirectedGraph<VertexProp>::edge_descriptor,
			bool>
		add_edge(vertex_descriptor u, vertex_descriptor v)
		{
			assert(u.index() < m_vertices.size());
			assert(v.index() < m_vertices.size());
			return (*this)[u].add_edge(v);
		}

		/** Remove the edge (u,v) from this graph. */
		void remove_edge(vertex_descriptor u, vertex_descriptor v)
		{
			(*this)[u].remove_edge(v);
		}

		/** Remove the edge e from this graph.
		 * Not implemented. */
		void remove_edge(edge_descriptor e)
		{
			assert(false);
		}

		/** Remove all out edges from vertex v. */
		void clear_out_edges(vertex_descriptor v)
		{
			(*this)[v].clear_out_edges();
		}

		/** Remove all edges to and from vertex u from this graph.
		 * Not implemented. */
		void clear_vertex(vertex_descriptor u)
		{
			assert(false);
		}

		/** Remove vertex v from this graph. It is assumed that there
		 * are no edges to or from vertex v. It is best to call
		 * clear_vertex before remove_vertex.
		 */
		void remove_vertex(vertex_descriptor v)
		{
			unsigned i = v.index();
			if (i >= m_removed.size())
				m_removed.resize(i + 1);
			m_removed[i] = true;
		}

		/** Return the number of vertices. */
		size_t num_vertices() const { return m_vertices.size(); }

		/** Return the number of edges. */
		size_t num_edges() const
		{
			size_t n = 0;
			for (vertex_iterator it = begin(); it != end(); ++it)
				n += it->out_degree();
			return n;
		}

		/** Return the out degree of the specified vertex. */
		degree_size_type out_degree(vertex_descriptor v) const
		{
			return (*this)[v].out_degree();
		}

		/** Return the nth vertex. */
		static vertex_descriptor vertex(vertices_size_type n)
		{
			return vertex_descriptor(n);
		}

		/** Return the source vertex of the specified edge.
		 * Not implemented.
		 */
		static vertex_descriptor source(edge_descriptor e)
		{
			assert(false);
			return vertex_descriptor(0);
		}

		/** Return the target vertex of the specified edge. */
		static vertex_descriptor target(edge_descriptor e)
		{
			return e->target();
		}

		friend std::ostream& operator <<(std::ostream& out,
				const DirectedGraph<VertexProp>& g)
		{
			for (vertex_iterator v = g.begin(); v != g.end(); ++v) {
				const vertex_descriptor& id = *v;
				if (g.is_removed(id))
					continue;
				if (sizeof (VertexProp) > 0)
					out << '"' << id << "\" ["
						<< v->get_property() << "]\n";
				if (v->out_degree() == 0)
					continue;
				out << '"' << id << "\" ->";
				if (v->out_degree() > 1)
					out << " {";
				for (out_edge_iterator e = v->begin();
						e != v->end(); ++e)
					out << " \"" << g.target(*e) << '"';
				if (v->out_degree() > 1)
					out << " }";
				out << '\n';
			}
			return out;
		}

	protected:
		/** Return true if this vertex has been removed. */
		bool is_removed(vertex_descriptor v) const
		{
			unsigned i = v.index();
			return i < m_removed.size() ? m_removed[i] : false;
		}

	private:
		DirectedGraph(const DirectedGraph& x);
		DirectedGraph& operator =(const DirectedGraph& x);

		/** The set of vertices. */
		Vertices m_vertices;

		/** Flags indicating vertices that have been removed. */
		std::vector<bool> m_removed;
};

#endif
