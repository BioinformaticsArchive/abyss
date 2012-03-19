#ifndef FMINDEX_H
#define FMINDEX_H 1

#include "BitArrays.h"
#include <algorithm>
#include <cassert>
#include <climits> // for UCHAR_MAX
#include <fstream>
#include <istream>
#include <limits> // for numeric_limits
#include <ostream>
#include <stdint.h>
#include <string>
#include <vector>

/** A match of a substring of a query sequence to an FM index. */
struct FMInterval
{
	FMInterval(size_t l, size_t u, unsigned qstart, unsigned qend)
		: l(l), u(u), qstart(qstart), qend(qend) { }
	size_t l, u;
	unsigned qstart, qend;

	unsigned qspan() const
	{
		assert(qstart <= qend);
		return qend - qstart;
	}
};

/** A match of a substring of a query sequence to a target sequence.
 */
struct Match
{
	unsigned qstart, qend;
	size_t tstart;
	unsigned count;
	Match(unsigned qstart, unsigned qend,
			size_t tstart, unsigned count)
		: qstart(qstart), qend(qend), tstart(tstart), count(count) { }

	unsigned qspan() const
	{
		assert(qstart <= qend);
		return qend - qstart;
	}
};

/** An FM index. */
class FMIndex
{
	/** An index. */
	typedef uint32_t size_type;

	/** A symbol. */
	typedef uint8_t T;

	/** The sentinel symbol. */
	static T SENTINEL() { return std::numeric_limits<T>::max(); }

  public:
	void read(const char *path, std::vector<T> &s);
	void buildFmIndex(const char *path, unsigned sampleSA);
	size_t locate(size_t i) const;

	FMIndex() : m_sampleSA(0) { }

	/** Construct an FMIndex. */
	FMIndex(const std::string& path) : m_sampleSA(0)
	{
		std::ifstream in(path.c_str());
		assert(in);
		in >> *this;
		assert(in);
	}

	/** Decompress the index. */
	template <typename It>
	void decompress(It out)
	{
		// Construct the original string.
		std::vector<T> s;
		for (size_t i = 0;;) {
			assert(i < m_occ.size());
			T c = m_occ.at(i);
			if (c == SENTINEL())
				break;
			s.push_back(c);
			i = m_cf[c] + m_occ.rank(c, i);
			assert(i > 0);
		}

		// Translate the character set and output the result.
		for (std::vector<T>::const_reverse_iterator it = s.rbegin();
				it != s.rend(); ++it) {
			T c = *it;
			assert(c < m_alphabet.size());
			*out++ = m_alphabet[c];
		}
	}

	/** Search for an exact match. */
	template <typename It>
	std::pair<size_t, size_t> findExact(It first, It last) const
	{
		assert(first < last);
		size_t l = 1, u = m_occ.size();
		It it;
		for (it = last - 1; it >= first && l < u; --it) {
			T c = *it;
			if (c == UCHAR_MAX)
				return std::make_pair(0, 0);
			l = m_cf[c] + m_occ.rank(c, l);
			u = m_cf[c] + m_occ.rank(c, u);
		}
		return std::make_pair(l, u);
	}

	/** Search for an exact match. */
	template <typename T>
	std::pair<size_t, size_t> findExact(const T& q) const
	{
		T s(q.size());
		std::transform(q.begin(), q.end(), s.begin(),
				Translate(*this));
		return findExact(s.begin(), s.end());
	}

	/** Search for a matching suffix of the query. */
	template <typename It>
	FMInterval findSuffix(It first, It last) const
	{
		assert(first < last);
		size_t l = 1, u = m_occ.size();
		It it;
		for (it = last - 1; it >= first && l < u; --it) {
			T c = *it;
			if (c == UCHAR_MAX)
				break;
			size_t l1 = m_cf[c] + m_occ.rank(c, l);
			size_t u1 = m_cf[c] + m_occ.rank(c, u);
			if (l1 >= u1)
				break;
			l = l1;
			u = u1;
		}
		return FMInterval(l, u, it - first + 1, last - first);
	}

	/** Search for a matching substring of the query at least k long.
	 * @return the longest match
	 */
	template <typename It>
	FMInterval findSubstring(It first, It last, unsigned k) const
	{
		assert(first < last);
		FMInterval best(0, 0, 0, k > 0 ? k - 1 : 0);
		for (It it = last; it > first; --it) {
			if (unsigned(it - first) < best.qspan())
				return best;
			FMInterval interval = findSuffix(first, it);
			if (interval.qspan() > best.qspan())
				best = interval;
		}
		return best;
	}

	/** Translate from ASCII to the indexed alphabet. */
	struct Translate {
		Translate(const FMIndex& fmIndex) : m_fmIndex(fmIndex) { }
		T operator()(unsigned char c) const
		{
			return c < m_fmIndex.m_mapping.size()
				? m_fmIndex.m_mapping[c] : UCHAR_MAX;
		}
	  private:
		const FMIndex& m_fmIndex;
	};

	/** Search for a matching substring of the query at least k long.
	 * @return the longest match and the number of matches
	 */
	Match find(const std::string& q, unsigned k) const
	{
		std::string s = q;
		std::transform(s.begin(), s.end(), s.begin(),
				Translate(*this));

		FMInterval interval = findSubstring(s.begin(), s.end(), k);
		assert(interval.l <= interval.u);
		size_t count = interval.u - interval.l;
		if (count == 0)
			return Match(0, 0, 0, 0);
		return Match(interval.qstart, interval.qend,
				locate(interval.l), count);
	}

	/** Set the alphabet to [first, last). */
	template <typename It>
	void setAlphabet(It first, It last)
	{
		std::vector<bool> mask(std::numeric_limits<T>::max());
		for (It it = first; it < last; ++it) {
			assert((size_t)*it < mask.size());
			mask[*it] = true;
		}

		m_alphabet.clear();
		m_mapping.clear();
		for (unsigned c = 0; c < mask.size(); ++c) {
			if (!mask[c])
				continue;
			m_mapping.resize(c + 1, std::numeric_limits<T>::max());
			m_mapping[c] = m_alphabet.size();
			m_alphabet.push_back(c);
		}
		assert(!m_alphabet.empty());
	}

	/** Set the alphabet to the characters of s. */
	void setAlphabet(const std::string& s)
	{
		setAlphabet(s.begin(), s.end());
	}

	/** The version number. */
	static const char* FM_VERSION() { return "FM 1"; }

/** Store an index. */
friend std::ostream& operator<<(std::ostream& out, const FMIndex& o)
{
	out << FM_VERSION() << '\n'
		<< o.m_sampleSA << '\n';

	out << o.m_alphabet.size() << '\n';
	out.write((char*)o.m_alphabet.data(),
			o.m_alphabet.size() * sizeof o.m_alphabet[0]);

	out << o.m_cf.size() << '\n';
	out.write((char*)o.m_cf.data(), o.m_cf.size() * sizeof o.m_cf[0]);

	out << o.m_sampledSA.size() << '\n';
	out.write((char*)o.m_sampledSA.data(),
		o.m_sampledSA.size() * sizeof o.m_sampledSA[0]);

	return out << o.m_occ;
}

/** Load an index. */
friend std::istream& operator>>(std::istream& in, FMIndex& o)
{
	std::string version;
	std::getline(in, version);
	assert(in);
	assert(version == FM_VERSION());

	in >> o.m_sampleSA;
	assert(in);

	size_t n;
	char c;

	in >> n;
	assert(in);
	c = in.get();
	assert(c == '\n');
	assert(n < std::numeric_limits<size_type>::max());
	o.m_alphabet.resize(n);
	in.read((char*)o.m_alphabet.data(), n * sizeof o.m_alphabet[0]);

	in >> n;
	assert(in);
	c = in.get();
	assert(c == '\n');
	assert(n < std::numeric_limits<size_type>::max());
	o.m_cf.resize(n);
	in.read((char*)o.m_cf.data(), n * sizeof o.m_cf[0]);

	in >> n;
	assert(in);
	c = in.get();
	assert(c == '\n');
	assert(n < std::numeric_limits<size_type>::max());
	o.m_sampledSA.resize(n);
	in.read((char*)o.m_sampledSA.data(), n * sizeof o.m_sampledSA[0]);

	o.setAlphabet(o.m_alphabet.begin(), o.m_alphabet.end());

	return in >> o.m_occ;
}

  private:
	void calculateStatistics(const std::vector<T> &s);
	void buildBWT(const std::vector<T> &s,
			const std::vector<size_type> &sa,
			std::vector<T> &bwt);
	void buildSA(const std::vector<T> &s,
			std::vector<size_type> &sa);
	void buildSampledSA(const std::vector<size_type> &sa);

	unsigned m_sampleSA;
	std::vector<T> m_alphabet;
	std::vector<T> m_mapping;
	std::vector<size_type> m_cf;
	std::vector<size_type> m_sampledSA;
	BitArrays m_occ;
};

#endif
