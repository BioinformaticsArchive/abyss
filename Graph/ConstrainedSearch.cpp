#include "ConstrainedSearch.h"
#include "ContigGraph.h"
#include "DirectedGraph.h"
#include <algorithm>
#include <climits> // for INT_MIN
#include <utility>

using namespace std;

namespace opt {
	/** Abort the search after visiting maxCost vertices. */
	unsigned maxCost = 100000;
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
	Constraints::iterator it = lower_bound(
			constraints.begin(), constraints.end(),
			key, compareID);
	return it->first == key ? it : constraints.end();
}

typedef Graph::vertex_descriptor vertex_descriptor;
typedef Graph::adjacency_iterator adjacency_iterator;

/** Find paths through the graph that satisfy the constraints.
 * @return false if the search exited early
 */
bool constrainedSearch(const Graph& g,
		vertex_descriptor v,
		Constraints& constraints,
		Constraints::const_iterator nextConstraint,
		unsigned satisfied,
		ContigPath& path, ContigPaths& solutions,
		size_t currLen, unsigned& visitedCount)
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
			if (!constrainedSearch(g, v, constraints,
						nextConstraint, satisfied, path, solutions,
						currLen, visitedCount))
				return false;
			it->second = constraint;
			return true;
		}
		currLen += g[path.back()].length - opt::k + 1;
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

	path.push_back(vertex_descriptor());
	pair<adjacency_iterator, adjacency_iterator>
		adj = g.adjacent_vertices(v);
	for (adjacency_iterator it = adj.first; it != adj.second; ++it) {
		path.back() = *it;
		if (!constrainedSearch(g, path.back(), constraints,
					nextConstraint, satisfied, path, solutions,
					currLen, visitedCount))
			return false;
	}
	assert(!path.empty());
	path.pop_back();
	return true;
}

/** Find paths through the graph that satisfy the constraints.
 * @return false if the search exited early
 */
bool constrainedSearch(const Graph& g, vertex_descriptor v,
		Constraints& constraints, ContigPaths& paths,
		unsigned& cost)
{
    if (constraints.empty())
            return false;

	// Sort the constraints by ID.
	sort(constraints.begin(), constraints.end());

	// Sort the constraints by distance.
	Constraints queue(constraints);
	sort(queue.begin(), queue.end(), compareDistance);

	ContigPath path;
	constrainedSearch(g, v, constraints, queue.begin(), 0,
			path, paths, 0, cost);
	return cost >= opt::maxCost ? false : !paths.empty();
}
