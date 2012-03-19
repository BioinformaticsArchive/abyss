#include "DirectedGraph.h"
#include "ContigNode.h"
#include <algorithm>
#include <climits> // for INT_MIN
#include <utility>

namespace opt {
	extern unsigned k;
	extern unsigned maxCost;
	static const unsigned maxPaths = 200;
};

template<typename K, typename D>
void Vertex<K,D>::addEdge(VertexType* pNode, extDirection dir,
		bool reverse)
{
	EdgeData edge(pNode, reverse);
	for (typename EdgeCollection::const_iterator edgeIter
			= m_edges[dir].begin();
			edgeIter != m_edges[dir].end(); ++edgeIter)
		assert(!(*edgeIter == edge));
	m_edges[dir].push_back(edge);
}

template<typename D>
void DirectedGraph<D>::addEdge(
		const LinearNumKey& parent, extDirection dir,
		const ContigNode& child)
{
	VertexType* pParentVertex = findVertex(parent);
	assert(pParentVertex != NULL);
	VertexType* pChildVertex = findVertex(child.id());
	assert(pChildVertex != NULL);
	pParentVertex->addEdge(pChildVertex, dir, child.sense());
}

template<typename D>
void DirectedGraph<D>::addVertex(const LinearNumKey& key,
		const D& data)
{
	assert(m_vertexTable.size() == key);
	m_vertexTable.push_back(VertexType(key, data));
}

template<typename D>
typename DirectedGraph<D>::VertexType* DirectedGraph<D>::findVertex(
		const LinearNumKey& key)
{
	assert(key < m_vertexTable.size());
	return &m_vertexTable[key];
}

template<typename D>
const typename DirectedGraph<D>::VertexType* DirectedGraph<D>::
findVertex(const LinearNumKey& key) const
{
	assert(key < m_vertexTable.size());
	return &m_vertexTable[key];
}

// Count all the edges in all the nodes
template<typename D>
size_t DirectedGraph<D>::countEdges() const
{
	size_t sum = 0;
	for (typename VertexTable::const_iterator it
			= m_vertexTable.begin(); it != m_vertexTable.end(); ++it)
		sum += it->numEdges(false) + it->numEdges(true);
	return sum;
}

/** Compare the distance of two constraints. */
static inline bool compareDistance(
		const Constraint& a, const Constraint& b)
{
	return a.second < b.second;
}

/** Compare the ID of a constraint. */
static inline bool compareID(const Constraint& constraint,
		const ContigNode& key)
{
	return constraint.first < key;
}

/** Find a constraint by ID. */
static inline Constraints::iterator findConstraint(
		Constraints& constraints,
		const ContigNode& key)
{
	Constraints::iterator it = std::lower_bound(
			constraints.begin(), constraints.end(),
			key, compareID);
	return it->first == key ? it : constraints.end();
}

/** Find paths through the graph that satisfy the constraints.
 * @return false if the search exited early
 */
template<typename D>
bool DirectedGraph<D>::findSuperpaths(const LinearNumKey& sourceKey,
		extDirection dir, Constraints& constraints,
		ContigPaths& superPaths, unsigned& compCost) const
{
    if (constraints.empty())
            return false;

	// Sort the constraints by ID.
	std::sort(constraints.begin(), constraints.end());

	// Sort the constraints by distance.
	Constraints queue(constraints);
	std::sort(queue.begin(), queue.end(), compareDistance);

	ContigPath path;
	ConstrainedDFS(findVertex(sourceKey), dir,
			constraints, queue.begin(), 0,
			path, superPaths, 0, compCost);
	return compCost >= opt::maxCost ? false : !superPaths.empty();
}

/** Find paths through the graph that satisfy the constraints.
 * @return false if the search exited early
 */
template<typename D>
bool DirectedGraph<D>::ConstrainedDFS(const VertexType* pCurrVertex,
		extDirection dir, Constraints& constraints,
		Constraints::const_iterator nextConstraint,
		unsigned satisfied,
		ContigPath& path, ContigPaths& solutions,
		size_t currLen, unsigned& visitedCount) const
{
	assert(satisfied < constraints.size());
	static const unsigned SATISFIED = UINT_MAX;
	if (!path.empty()) {
		Constraints::iterator it = findConstraint(
				constraints, path.back());
		if (it != constraints.end() && it->second != SATISFIED) {
			if (currLen > it->second)
				return true; // This constraint cannot be met.
			if (++satisfied == constraints.size()) {
				// All the constraints have been satisfied.
				solutions.push_back(path);
				return solutions.size() <= opt::maxPaths;
			}
			// This constraint has been satisfied.
			unsigned constraint = it->second;
			it->second = SATISFIED;
			if (!ConstrainedDFS(pCurrVertex, dir,
						constraints, nextConstraint, satisfied,
						path, solutions, currLen, visitedCount))
				return false;
			it->second = constraint;
			return true;
		}
		currLen += pCurrVertex->m_data;
	}

	if (++visitedCount >= opt::maxCost)
		return false; // Too complex.

	// Check that the next constraint has not been violated.
	while (currLen > nextConstraint->second
			&& findConstraint(constraints,
				nextConstraint->first)->second == SATISFIED)
		++nextConstraint; // This constraint is satisfied.
	if (currLen > nextConstraint->second)
		return true; // This constraint cannot be met.

	bool isRC = path.empty() ? false : path.back().sense();
	const typename VertexType::EdgeCollection& currEdges
		= pCurrVertex->m_edges[isRC ^ dir];
	path.push_back(ContigNode());
	for (typename VertexType::EdgeCollection::const_iterator it
			= currEdges.begin(); it != currEdges.end(); ++it) {
		path.back() = ContigNode(
				it->pVertex->m_key, it->reverse ^ isRC);
		if (!ConstrainedDFS(it->pVertex, dir,
					constraints, nextConstraint, satisfied,
					path, solutions, currLen, visitedCount))
			return false;
	}
	assert(!path.empty());
	path.pop_back();
	return true;
}

template<typename D>
size_t DirectedGraph<D>::calculatePathLength(const ContigPath& path)
	const
{
	size_t len = 0;
	for (typename ContigPath::const_iterator it = path.begin();
			it != path.end() - 1; ++it)
		len += (*this)[it->id()].m_data;
	return len;
}

/** Return a map of contig IDs to their distance along this path.
 * Repeat contigs, which would have more than one position, are not
 * represented in this map.
 */
template<typename D>
void DirectedGraph<D>::makeDistanceMap(const ContigPath& path,
		std::map<ContigNode, int>& distanceMap) const
{
	size_t distance = 0;
	for (typename ContigPath::const_iterator iter = path.begin();
			iter != path.end(); ++iter) {
		bool inserted = distanceMap.insert(
				std::make_pair(*iter, distance)).second;
		if (!inserted) {
			// Mark this contig as a repeat.
			distanceMap[*iter] = INT_MIN;
		}
		distance += (*this)[iter->id()].m_data;
	}

	// Remove the repeats.
	for (std::map<ContigNode, int>::iterator it
			= distanceMap.begin(); it != distanceMap.end();)
		if (it->second == INT_MIN)
			distanceMap.erase(it++);
		else
			++it;
}
