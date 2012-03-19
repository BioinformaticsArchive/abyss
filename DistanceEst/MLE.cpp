#include "MLE.h"
#include "PDF.h"
#include <algorithm> // for swap
#include <cassert>
#include <limits> // for numeric_limits

using namespace std;

namespace opt {
	extern unsigned k;
}

/** This window function is a triangle with a flat top, or a rectangle
 * with sloped sides.
 */
class WindowFunction {
	public:
		WindowFunction(int len0, int len1, int d)
		{
			assert(len0 > 0);
			assert(len1 > 0);
			assert(len0 <= len1);
			if (d < 0)
				d = 0;
			x0 = d;
			x1 = d + len0;
			x2 = d + len1;
			x3 = d + len0 + len1;
			y = len0;
		}

		/** Return this window function evaluated at x. */
		double operator ()(int x) const
		{
			return (x <= x0 ? 1
					: x < x1 ? x - x0
					: x < x2 ? y
					: x < x3 ? x3 - x
					: 1) / (double)y;
		}

	private:
		/** Parameters of this window function. */
		int x0, x1, x2, x3, y;
};

/** Compute the log likelihood that these samples came from the
 * specified distribution shifted by the parameter theta and scaled by
 * the specified window function.
 * @param theta the parameter of the PDF
 * @param samples the samples
 * @param pdf the PDF scaled by the window function
 * @param window the window function used to scale the PDF
 * @param c the normalizing constant of the PMF
 * @param[out] n the number of samples with a non-zero probability
 * @return the log likelihood
 */
static double computeLikelihood(int theta, const Histogram& samples,
		const PDF& pdf, const WindowFunction& window, double c,
		unsigned &n)
{
	n = 0;
	double sum = 0.0f;
	for (Histogram::const_iterator it = samples.begin();
			it != samples.end(); ++it) {
		int x = it->first + theta;

		/* When randomly selecting fragments that span a given point,
		 * longer fragments are more likely to be selected than
		 * shorter fragments.
		 */
		sum += it->second * log(pdf[x] * window(x) / c);
		if (pdf[x] > pdf.getMinP())
			n += it->second;
	}
	return sum;
}

/** Return the most likely distance between two contigs. */
static int maximumLikelihoodEstimate(int first, int last,
		const Histogram& samples,
		const PDF& pdf,
		unsigned len0, unsigned len1,
		unsigned &n)
{
	double bestLikelihood = -numeric_limits<double>::max();
	int bestTheta = first;
	unsigned bestn = 0;
	for (int theta = first; theta < last; theta++) {
		// Calculate the normalizing constant of the PMF.
		WindowFunction window(len0, len1, theta);
		double c = 0;
		for (int i = first; i < last; ++i)
			c += pdf[i] * window(i);

		unsigned trialn;
		double likelihood = computeLikelihood(theta, samples,
				pdf, window, c, trialn);
		if (likelihood > bestLikelihood) {
			bestLikelihood = likelihood;
			bestTheta = theta;
			bestn = trialn;
		}
	}
	n = bestn;
	return bestTheta;
}

/** Return the most likely distance between two contigs.
 * @param len0 the length of the first contig in bp
 * @param len1 the length of the second contig in bp
 * @param rf whether the fragment library is oriented reverse-forward
 */
int maximumLikelihoodEstimate(int first, int last,
		const vector<int>& samples, const PDF& pdf,
		unsigned len0, unsigned len1, bool rf,
		unsigned& n)
{
	// The aligner is unable to map reads to the ends of the sequence.
	// Correct for this lack of sensitivity by subtracting x from the
	// length of each sequence, where x is k-1 for an aligner that
	// requires a match of at least k bp. When the fragment library
	// is oriented forward-reverse, subtract 2*x from each sample.
	assert(first < 0);
	unsigned overlap = -first;
	assert(len0 > overlap);
	assert(len1 > overlap);
	len0 -= overlap;
	len1 -= overlap;

	if (len0 > len1)
		swap(len0, len1);

	if (rf) {
		// This library is oriented reverse-forward.
		Histogram h(samples.begin(), samples.end());
		return maximumLikelihoodEstimate(
				first, last, h,
				pdf, len0, len1, n);
	} else {
		// This library is oriented forward-reverse.
		// Subtract 2*x from each sample.
		Histogram h;
		typedef vector<int> Samples;
		for (Samples::const_iterator it = samples.begin();
				it != samples.end(); ++it) {
			assert(*it > 2 * (int)overlap);
			h.insert(*it - 2 * overlap);
		}
		int d = maximumLikelihoodEstimate(
				0, last, h,
				pdf, len0, len1, n);
		return max(first, d - 2 * (int)overlap);
	}
}
