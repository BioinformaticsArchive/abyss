#include "FastaReader.h"
#include "Options.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <limits> // for numeric_limits
#include <sstream>

using namespace std;

static void assert_open(ifstream& f, const string& p)
{
	if (f.is_open())
		return;
	cerr << p << ": " << strerror(errno) << endl;
	exit(EXIT_FAILURE);
}

FastaReader::FastaReader(const char* path)
	: m_inPath(path), m_inFile(path),
	m_fileHandle(strcmp(path, "-") == 0 ? cin : m_inFile),
	m_unchaste(0), m_nonacgt(0)
{
	if (strcmp(path, "-") != 0)
		assert_open(m_inFile, path);
	if (m_fileHandle.peek() == EOF)
		fprintf(stderr, "warning: `%s' is empty\n", path);
}

FastaReader::~FastaReader()
{
	m_inFile.close();
}

static bool isChaste(const string &s)
{
	if (s == "1" || s == "Y") {
		return true;
	} else if (s == "0" || s == "N") {
		return false;
	} else {
		cerr << "error: chastity filter should be either "
			<< "0, 1, N or Y and saw `" << s << "'\n";
		exit(EXIT_FAILURE);
	}
}

Sequence FastaReader::read(string& id, string& comment, char& anchor)
{
next_record:
	// Discard comments.
	while (m_fileHandle.peek() == '#') {
		m_fileHandle.ignore(numeric_limits<streamsize>::max(), '\n');
		if (m_fileHandle.peek() == EOF) {
			fputs("error: file ends in comments\n", stderr);
			assert(false);
		}
	}

	char recordType = m_fileHandle.peek();
	Sequence s;

	if (recordType == EOF) {
		m_fileHandle.get();
		return s;
	} else if (recordType == '>' || recordType == '@') {
		// Read the header.
		string header;
		getline(m_fileHandle, header);
		stringstream headerStream(header);
		headerStream >> recordType >> id;
		comment = headerStream.str();

		getline(m_fileHandle, s);
		transform(s.begin(), s.end(), s.begin(), ::toupper);

		assert(s.length() > 2);
		if (isalpha(s[0]) && isdigit(s[1])) {
			// The first character is the primer base. The second
			// character is the dibase read of the primer and the first
			// base of the sample, which is not part of the assembly.
			anchor = colourToNucleotideSpace(s.at(0), s.at(1));
			s = s.substr(2);
		}

		if (recordType == '@') {
			// Discard the quality values.
			char c = m_fileHandle.get();
			assert(c == '+');
			m_fileHandle.ignore(numeric_limits<streamsize>::max(), '\n');
			m_fileHandle.ignore(numeric_limits<streamsize>::max(), '\n');
		}
	} else {
		string line;
		vector<string> fields;
		fields.reserve(22);
		getline(m_fileHandle, line);
		istringstream in(line);
		string field;
		while (getline(in, field, '\t'))
			fields.push_back(field);

		if (fields.size() == 11 || fields.size() == 22) {
			if (opt::chastityFilter && !isChaste(fields.back())) {
				m_unchaste++;
				goto next_record;
			}

			ostringstream o(fields[0]);
			for (int i = 1; i < 6; i++)
				o << '_' << fields[i];
			o << '/' << fields[7];
			id = o.str();
			s = fields[8];
			assert(s.length() > 2);
		} else {
			fprintf(stderr, "error: `%s' is an unknown format\n"
					"Expected either `>' or `@' or 11 fields\n"
					"and saw `%c' and %zu fields\n",
					m_inPath, recordType, fields.size());
			exit(EXIT_FAILURE);
		}
	}

	size_t pos = s.find_first_not_of("ACGT0123");
	if (pos != std::string::npos) {
		if (opt::verbose > 4)
			fprintf(stderr,
					"warning: discarded sequence containing `%c'\n",
					s[pos]);
		m_nonacgt++;
		goto next_record;
	}

	return s;
}
