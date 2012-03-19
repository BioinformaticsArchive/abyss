#include "FastaReader.h"
#include "Options.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <iostream>

using namespace std;

FastaReader::FastaReader(const char* path)
	: m_inPath(path), m_inFile(path),
	m_fileHandle(strcmp(path, "-") == 0 ? cin : m_inFile),
	m_nonacgt(0)
{
	if (strcmp(path, "-") != 0)
		assert(m_inFile.is_open());
	if (m_fileHandle.peek() == EOF)
		fprintf(stderr, "warning: `%s' is empty\n", path);
}

FastaReader::~FastaReader()
{
	m_inFile.close();
}

Sequence FastaReader::ReadSequence(string& id)
{
	// Discard comments.
	while (m_fileHandle.peek() == '#') {
		m_fileHandle.ignore(numeric_limits<streamsize>::max(), '\n');
		if (m_fileHandle.peek() == EOF) {
			fputs("error: file ends in comments\n", stderr);
			assert(false);
		}
	}

	// Read the header.
	char recordType;
	m_fileHandle >> recordType >> id;
	m_fileHandle.ignore(numeric_limits<streamsize>::max(), '\n');

	Sequence s;
	getline(m_fileHandle, s);
	transform(s.begin(), s.end(), s.begin(), ::toupper);

	assert(s.length() > 2);
	if (isdigit(s[1])) {
		// The first character is the primer base. The second
		// character is the dibase read of the primer and the first
		// base of the sample, which is not part of the assembly.
		assert(isalpha(s[0]));
		s = s.substr(2);
		opt::colourSpace = true;
	} else
		assert(!opt::colourSpace);

	if (recordType == '>') {
		// Nothing to do.
	} else if (recordType == '@') {
		// Discard the quality values.
		char c;
		m_fileHandle >> c;
		assert(c == '+');
		m_fileHandle.ignore(numeric_limits<streamsize>::max(), '\n');
		m_fileHandle.ignore(numeric_limits<streamsize>::max(), '\n');
	} else {
		fprintf(stderr, "error: `%s' is an unknown format\n"
					"Expected either `>' or `@' and saw `%c'\n",
				m_inPath, recordType);
		exit(EXIT_FAILURE);
	}
	return s;
}

// Read in a group of sequences and return whether there are sequences remaining
bool FastaReader::ReadSequences(SequenceVector& outseqs)
{
	if (!isGood())
		return false;
	Sequence seq = ReadSequence();
	size_t pos = seq.find_first_not_of("ACGT0123");
	if (pos == std::string::npos) {
		outseqs.push_back(seq);
	} else {
		if (opt::verbose > 4)
			fprintf(stderr,
					"warning: discarded sequence containing `%c'\n",
					seq[pos]);
		m_nonacgt++;
	}
	return true;
}

bool FastaReader::isGood()
{
	return !(m_fileHandle.eof() || m_fileHandle.peek() == EOF);
}
