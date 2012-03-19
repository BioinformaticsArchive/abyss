#ifndef SEQEXT_H
#define SEQEXT_H 1

#include <stdint.h>

static const int NUM_BASES = 4;

static inline uint8_t complementBaseCode(uint8_t base)
{
    return ~base & 0x3;
}

class SeqExt
{
	public:
		SeqExt() : m_record(0) { };

		/** Set the specified adjacency. */
		void setBase(uint8_t base)
		{
			m_record |= 1 << base;
		}

		/** Clear the specified adjacency. */
		void clearBase(uint8_t base)
		{
			m_record &= ~(1 << base);
		}

		/** Return wheter the specified base is adjacent. */
		bool checkBase(uint8_t base) const
		{
			return m_record & (1 << base);
		}

		/** Clear all adjacency. */
		void clear()
		{
			m_record = 0;
		}

		/** Return whether this kmer has any adjacent kmer. */
		bool hasExtension() const
		{
			return m_record > 0;
		}

		/** Return whether this kmer has more than one adjacent kmer.
		 */
		bool isAmbiguous() const
		{
			bool powerOfTwo = (m_record & (m_record - 1)) > 0;
			return m_record > 0 && powerOfTwo;
		}

		/** Return the complementary adjacency. */
		SeqExt complement() const;

		void print() const;

	private:
		SeqExt(uint8_t ext) : m_record(ext) { };

		uint8_t m_record;
};

#endif
