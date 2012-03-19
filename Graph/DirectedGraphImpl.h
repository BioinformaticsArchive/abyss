#include "DirectedGraph.h"
#include <algorithm>
#include <climits> // for INT_MIN
#include <utility>

namespace opt {
	extern unsigned k;
	extern unsigned maxCost;
	static const unsigned maxPaths = 200;
};

template<typename K, typename D>
void Vertex<K,D>::addEdge(VertexType* pNode)
{
	EdgeData edge(pNode);
	for (typename EdgeCollection::const_iterator edgeIter
			= m_edges.begin();
			edgeIter != m_edges.end(); ++edgeIter)
		assert(!(*edgeIter == edge));
	m_edges.push_back(edge);
}

template<typename D>
void DirectedGraph<D>::addEdge(const Node& parent, const Node& child)
{
	VertexType* pParentVertex = findVertex(parent);
	assert(pParentVertex != NULL);
	VertexType* pChildVertex = findVertex(child);
	assert(pChildVertex != NULL);
	pParentVertex->addEdge(pChildVertex);
}

template<typename D>
void DirectedGraph<D>::addVertex(const Node& key,
		const D& data)
{
	assert(m_vertexTable.size() == key.index());
	m_vertexTable.push_back(VertexType(key, data));
}

/** Return the number of edges. */
template<typename D>
size_t DirectedGraph<D>::countEdges() const
{
	size_t sum = 0;
	for (typename VertexTable::const_iterator it
			= m_vertexTable.begin(); it != m_vertexTable.end(); ++it)
		sum += it->numEdges();
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
bool DirectedGraph<D>::findSuperpaths(const Node& sourceKey,
		Constraints& constraints,
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
	ConstrainedDFS(findVertex(sourceKey),
			constraints, queue.begin(), 0,
			path, superPaths, 0, compCost);
	return compCost >= opt::maxCost ? false : !superPaths.empty();
}

/** Find paths through the graph that satisfy the constraints.
 * @return false if the search exited early
 */
template<typename D>
bool DirectedGraph<D>::ConstrainedDFS(const VertexType* pCurrVertex,
		Constraints& constraints,
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
			if (!ConstrainedDFS(pCurrVertex,
						constraints, nextConstraint, satisfied,
						path, solutions, currLen, visitedCount))
				return false;
			it->second = constraint;
			return true;
		}
		currLen += pCurrVertex->m_key.length();
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

	const typename VertexType::EdgeCollection& currEdges
		= pCurrVertex->m_edges;
	path.push_back(Node());
	for (typename VertexType::EdgeCollection::const_iterator it
			= currEdges.begin(); it != currEdges.end(); ++it) {
		path.back() = it->pVertex->m_key;
		if (!ConstrainedDFS(it->pVertex,
					constraints, nextConstraint, satisfied,
					path, solutions, currLen, visitedCount))
			return false;
	}
	assert(!path.empty());
	path.pop_back();
	return true;
}

/** Return a map of contig IDs to their distance along this path.
 * Repeat contigs, which would have more than one position, are not
 * represented in this map.
 */
template<typename D>
void DirectedGraph<D>::makeDistanceMap(const ContigPath& path,
		std::map<Node, int>& distanceMap) const
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
		distance += iter->length();
	}

	// Remove the repeats.
	for (std::map<Node, int>::iterator it
			= distanceMap.begin(); it != distanceMap.end();)
		if (it->second == INT_MIN)
			distanceMap.erase(it++);
		else
			++it;
}
