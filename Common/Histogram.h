#ifndef HISTOGRAM_H
#define HISTOGRAM_H 1

#include <cassert>
#include <cmath>
#include <map>
#include <ostream>
#include <vector>

/** A histogram of type T, which is int be default.
 * A histogram may be implemented as a multiset. This class aims
 * to provide a similar interface to a multiset.
 */
class Histogram : std::map<int, unsigned>
{
  public:
	typedef int T;
	typedef std::map<T, unsigned> Map;

	Histogram() { }

	Histogram(std::vector<unsigned> v)
	{
		for (T i = 0; i < (T)v.size(); i++)
			if (v[i] > 0)
				Map::insert(std::make_pair(i, v[i]));
	}

	void insert(T value) { (*this)[value]++; }

	void insert(T value, unsigned count)
	{
		(*this)[value] += count;
	}

	unsigned count(T value) const
	{
		Map::const_iterator iter = find(value);
		return find(value) == end() ? 0 : iter->second;
	}

	T minimum() const
	{
		return empty() ? 0 : begin()->first;
	}

	T maximum() const
	{
		return empty() ? 0 : rbegin()->first;
	}

	using Map::empty;

	unsigned size() const
	{
		unsigned n = 0;
		for (Histogram::Map::const_iterator it = begin();
				it != end(); it++)
			n += it->second;
		return n;
	}

	double mean() const
	{
		unsigned n = 0, total = 0;
		for (Histogram::Map::const_iterator it = begin();
				it != end(); it++) {
			n += it->second;
			total += it->first * it->second;
		}
		return (double)total / n;
	}

	double variance() const
	{
		unsigned n = 0, total = 0, squares = 0;
		for (Histogram::Map::const_iterator it = begin();
				it != end(); it++) {
			n += it->second;
			total += it->first * it->second;
			squares += it->first * it->first * it->second;
		}
		return (squares - total * total / (double)n) / n;
	}

	double sd() const
	{
		return sqrt(variance());
	}

	/** Return the first local minimum. */
	T firstLocalMinimum() const
	{
		const unsigned SMOOTHING = 4;
		assert(!empty());
		Histogram::const_iterator minimum = begin();
		unsigned count = 0;
		for (Histogram::const_iterator it = begin();
				it != end(); ++it) {
			if (it->second <= minimum->second) {
				minimum = it;
				count = 0;
			} else if (++count >= SMOOTHING)
				break;
		}
		return minimum->first;
	}

	void eraseNegative()
	{
		for (Histogram::Map::iterator it = begin();
				it != end();)
			if (it->first < 0)
				erase(it++);
			else
				++it;
	}

	Histogram trim(double percent) const;

	/** Return a vector representing this histogram. */
	operator std::vector<unsigned>() const
	{
		assert(minimum() >= 0);
#if 0
		std::vector<unsigned> v(maximum()+1);
#else
		// CommLayer::reduce requires the arrays have the same size.
		std::vector<unsigned> v(2*65536);
		assert(maximum() < (T)v.size());
#endif
		for (Map::const_iterator it = begin(); it != end(); ++it)
			v[it->first] = it->second;
		return v;
	}

	friend std::ostream& operator<<(std::ostream& o,
			const Histogram& h)
	{
		for (Map::const_iterator it = h.begin();
				it != h.end(); ++it)
			o << it->first << '\t' << it->second << '\n';
		return o;
	}
};

#endif
