#ifndef OPTIONS_H
#define OPTIONS_H

#include <string>
#include <vector>

namespace opt {
	extern int kmerSize;
	extern int readLen;
	extern int erode;
	extern int trimLen;
	extern int bubbles;
	extern std::string graphPath;
	extern int verbose;
	extern std::vector<std::string> inFiles;

	void parse(int argc, char* const* argv);
}

#endif
