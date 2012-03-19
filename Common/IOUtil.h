#ifndef IOUTIL_H
#define IOUTIL_H 1

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring> // for strerror
#include <fstream>
#include <iostream>
#include <limits> // for numeric_limits
#include <string>

/** Print an error message and exit if stream is not good. */
static inline void assert_good(const std::ios& stream,
		const std::string& path)
{
	if (!stream.good()) {
		std::cerr << "error: `" << path << "': "
			<< strerror(errno) << std::endl;
		exit(EXIT_FAILURE);
	}
}

/** This input stream manipulator skips the specified string. */
struct expect {
	const char* s;
	expect(const char* s) : s(s) { }
};

static inline std::istream& operator>>(std::istream& in, expect o)
{
	for (const char* p = o.s; *p != '\0'; ++p) {
		if (*p == ' ') {
			in >> std::ws;
		} else {
			char c = in.get();
			if (!in || c != *p) {
				std::cerr << "error: Expected `" << p
					<< "' and saw ";
				if (in)
					std::cerr << '`' << c << "'\n";
				else if (in.eof())
					std::cerr << "end-of-file\n";
				else
					std::cerr << "I/O error\n";
				exit(EXIT_FAILURE);
			}
		}
	}
	return in;
}

/** This input stream manipulator discards characters until reaching
 * the delimeter. */
struct ignore {
	const char delim;
	size_t n;
	ignore(const char delim,
			size_t n = std::numeric_limits<std::streamsize>::max())
		: delim(delim), n(n) { }
};

static inline std::istream& operator>>(std::istream& in, ignore o)
{
	return in.ignore(o.n, o.delim);
}

/** Read a file and store it in the specified vector. */
template <typename Vector>
static inline void readFile(const char* path, Vector& s)
{
	std::ifstream in(path);
	assert_good(in, path);
	in.seekg(0, std::ios::end);
	s.resize(in.tellg());
	in.seekg(0, std::ios::beg);
	assert_good(in, path);
	in.read((char*)s.data(), s.size());
	assert_good(in, path);
	assert((size_t)in.gcount() == s.size());
}

#endif
