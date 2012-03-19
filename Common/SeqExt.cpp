#include "SeqExt.h"
#include <cassert>
#include <cstdio>

/** Return the complementary adjacency. */
SeqExt SeqExt::complement() const
{
	static const uint8_t complements[16] = {
		0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
		0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf
	};
	assert(m_record < 1<<NUM_BASES);
	return SeqExt(complements[m_record]);
}

void SeqExt::print() const
{
	assert(m_record < 1<<NUM_BASES);
	printf("ext: %c%c%c%c\n",
			 checkBase(3) ? 'T' : ' ',
			 checkBase(2) ? 'G' : ' ',
			 checkBase(1) ? 'C' : ' ',
			 checkBase(0) ? 'A' : ' ');
}
