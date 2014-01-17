/**
 * A Bloom filter
 * Copyright 2013 Shaun Jackman
 */
#ifndef BLOOMFILTER_H
#define BLOOMFILTER_H 1

#include "BloomFilterBase.h"
#include "Common/HashFunction.h"
#include "Common/Kmer.h"
#include "Common/IOUtil.h"
#include <algorithm>
#include <vector>
#include <iostream>

/** A Bloom filter. */
class BloomFilter : public virtual BloomFilterBase
{
  public:

	/** Constructor. */
	BloomFilter() { }

	/** Constructor. */
	BloomFilter(size_t n) : m_array(n) { }

	/** Return the size of the bit array. */
	size_t size() const { return m_array.size(); }

	/** Return the population count, i.e. the number of set bits. */
	size_t popcount() const
	{
		return std::count(m_array.begin(), m_array.end(), true);
	}

	/** Return the estimated false positive rate */
	double FPR() const
	{
		return (double)popcount() / size();
	}

	/** Return whether the specified bit is set. */
	virtual bool operator[](size_t i) const
	{
		assert(i < m_array.size());
		return m_array[i];
	}

	/** Return whether the object is present in this set. */
	virtual bool operator[](const key_type& key) const
	{
		return m_array[hash(key) % m_array.size()];
	}

	/** Add the object with the specified index to this set. */
	virtual void insert(size_t index)
	{
		assert(index < m_array.size());
		m_array[index] = true;
	}

	/** Add the object to this set. */
	void insert(const key_type& key)
	{
		m_array[hash(key) % m_array.size()] = true;
	}

	friend std::istream& operator>>(std::istream& in, BloomFilter& o)
	{
		o.read(in, false);
		return in;
	}

	friend std::ostream& operator<<(std::ostream& out, const BloomFilter& o)
	{
		out << BLOOM_VERSION << '\n';
		out << Kmer::length() << '\n';

		o.writeBloomDimensions(out);
		assert(out);

		size_t bits = o.size();
		size_t bytes = (bits + 7) / 8;
		for (size_t i = 0, j = 0; i < bytes; i++) {
			uint8_t byte = 0;
			for (unsigned k = 0; k < 8; k++, j++) {
				byte <<= 1;
				if (j < bits && o.m_array[j]) {
					byte |= 1;
				}
			}
			out << byte;
		}
		return out;
	}

	void read(std::istream& in, bool union_, unsigned shrinkFactor = 1)
	{
		unsigned bloomVersion;
		in >> bloomVersion >> expect("\n");
		assert(in);
		if (bloomVersion != BLOOM_VERSION) {
			std::cerr << "error: bloom filter version (`"
				<< bloomVersion << "'), does not match version required "
				"by this program (`" << BLOOM_VERSION << "').\n";
			exit(EXIT_FAILURE);
		}

		unsigned k;
		in >> k >> expect("\n");
		assert(in);
		if (k != Kmer::length()) {
			std::cerr << "error: this program must be run with the same kmer "
				"size as the bloom filter being loaded (k="
				<< k << ").\n";
			exit(EXIT_FAILURE);
		}

		size_t size, startBitPos, endBitPos;
		in >> size
		   >> expect("\t") >> startBitPos
		   >> expect("\t") >> endBitPos
		   >> expect("\n");

		assert(in);
		assert(startBitPos < size);
		assert(endBitPos < size);

		if (size % shrinkFactor != 0) {
			std::cerr << "error: the number of bits in the original bloom "
				"filter must be evenly divisible by the shrink factor (`"
				<< shrinkFactor << "')\n";
			exit(EXIT_FAILURE);
		}

		size /= shrinkFactor;
		size_t offset = startBitPos;
		size_t bits = endBitPos - startBitPos + 1;

		if(union_ && size != this->size()) {
			std::cerr << "error: can't union bloom filters "
				"with different sizes.\n";
			exit(EXIT_FAILURE);
		} else {
			m_array.resize(size);
		}

		if (!union_)
			m_array.assign(size, 0);

		size_t bytes = (bits + 7) / 8;
		for (size_t i = 0, j = offset; i < bytes; i++) {
			uint8_t byte;
			in.read((char *)&byte, 1);
			assert(in);
			for (k = 0; k < 8 && j < offset + bits; k++, j++) {
				bool bit = byte & (1 << (7 - k));
				size_t index = j % size;
				m_array[index] = m_array[index] | bit;
			}
		}
	}

  protected:

	std::vector<bool> m_array;

	virtual void writeBloomDimensions(std::ostream& out) const
	{
		out << size()
			<< '\t' << 0
			<< '\t' << size() - 1
			<< '\n';
	}

  private:

	static const unsigned BLOOM_VERSION = 2;

};

#endif
