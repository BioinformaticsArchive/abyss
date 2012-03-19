#include "dialign.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>
#include <iostream>

using namespace std;

static void free_scr_matrix(struct scr_matrix* smatrix)
{
	free(smatrix->dist);
	free(smatrix->data);
	free(smatrix->char2num);
	free(smatrix->num2char);
	free(smatrix);
}

void free_prob_dist(struct prob_dist* pdist)
{
	unsigned int length = pdist->max_dlen;
	unsigned int i;
	for (i=1; i<=length; i++) {
		free(pdist->data[i]);
		free(pdist->log_data[i]);
	}
	free(pdist->data);
	free(pdist->log_data);
	free_scr_matrix(pdist->smatrix);
	free(pdist);
}

void free_seq_col(struct seq_col* scol)
{
	unsigned int length = scol->length;
	unsigned int i;
	for (i=0; i<length; i++)
		free((scol->seqs[i]).data);
	free(scol->seqs);
	free(scol);
}

struct seq_col* read_seqs(const vector<string>& amb_seqs)
{
	struct seq_col* scol = (struct seq_col*)calloc(1, sizeof(struct seq_col));
	struct seq* seqs = (scol->seqs = (struct seq*)calloc(amb_seqs.size(), sizeof(struct seq)));
	if(scol==NULL || seqs==NULL) {
		cerr << "read_seqs(): Out of memory !\n";
		exit(EXIT_FAILURE);
	}
	scol->length = amb_seqs.size();
	scol->avg_length = 0;

	seq* seq;
	for (size_t i=0; i<amb_seqs.size(); i++) {
		seq = &(scol->seqs[i]);
		seq->max_seen = 0;
		//seq->name = calloc(rlen, sizeof(char)); //do I need this?
		seq->num = i;
		seq->orf_frame=0;
		seq->crick_strand=0;
		//strncpy(seq->name, &(rline[1]), rlen-2);
		seq->data = (char*)calloc(amb_seqs[i].length()+1, sizeof(char));
		if (seq->data == NULL) {
			cerr << "seq->data out of memory !\n";
			exit(EXIT_FAILURE);
		}
		strcpy(seq->data, amb_seqs[i].c_str());
		seq->length = amb_seqs[i].length();
		scol->avg_length += amb_seqs[i].length();
		if(para->DEBUG >1) printf("DEBUG: seq:%s\n", seq->data);
	}
	scol->avg_length /= scol->length;
	if(para->DEBUG >1) printf("DEBUG: total # of amb_seqs: %i, avg_length: %i\n", scol->length, scol->avg_length);
	return scol;
}

static map<string, char> initIUPAC()
{
	typedef map<string, char> Map;
	Map iupac;
	iupac.insert(Map::value_type("AG", 'R'));
	iupac.insert(Map::value_type("CT", 'Y'));
	iupac.insert(Map::value_type("AC", 'M'));
	iupac.insert(Map::value_type("GT", 'K'));
	iupac.insert(Map::value_type("CG", 'S'));
	iupac.insert(Map::value_type("AT", 'W'));
	iupac.insert(Map::value_type("ACT", 'H'));
	iupac.insert(Map::value_type("CGT", 'B'));
	iupac.insert(Map::value_type("ACG", 'V'));
	iupac.insert(Map::value_type("AGT", 'D'));
	//also reverse order for pairwise consensus
	iupac.insert(Map::value_type("GA", 'R'));
	iupac.insert(Map::value_type("TC", 'Y'));
	iupac.insert(Map::value_type("CA", 'M'));
	iupac.insert(Map::value_type("TG", 'K'));
	iupac.insert(Map::value_type("GC", 'S'));
	iupac.insert(Map::value_type("TA", 'W'));
	return iupac;
}

static char IUPAC(char* amb_chars, unsigned int size)
{
	static map<string, char> IUPAC_codes = initIUPAC();
	if (size == 1)
		return amb_chars[0];
	if (size == 0 || size == 4)
		return 'N';
	string amb_seq = amb_chars;
	map<string, char>::iterator it = IUPAC_codes.find(amb_seq);
	assert(it != IUPAC_codes.end());
	return it->second;
}

static char ind2char(unsigned int index)
{
	char c = 0;
	switch (index) {
	case 0:
		c = '-'; break;
	case 1:
		c = 'A'; break;
	case 2:
		c = 'C'; break;
	case 3:
		c = 'G'; break;
	case 4:
		c = 'T'; break;
	case 5:
		c = 'N'; break;
	default:
		cerr << "ind2char index out of range!\n";
		exit(EXIT_FAILURE);
	}

	return c;
}

/** Return the consensus base.
 * @param counts a count of the characters "-ACGTN"
 */
static char make_consensus(unsigned counts[6])
{
	char bases[5], *p = bases;
	for (unsigned i = 1; i < 5; i++)
		if (counts[i] > 0)
			*p++ = ind2char(i);
	*p = '\0';
	char c = IUPAC(bases, p - bases);
	return counts[0] > 0 ? tolower(c) : c;
}

// assume initial sequences contain only a/c/g/t/n
void get_alignment_consensus(struct alignment *algn, string& consensus)
{
  struct seq_col *scol = algn->scol;
  unsigned int slen = scol->length;

  int j;
  unsigned int s,max;
  struct seq* sq;
  struct algn_pos **ap = algn->algn;

  prepare_alignment(algn);
  max = algn->max_pos;
  if (para->DEBUG > 5) printf("slen is %u, max pos is %u\n", slen, max);
  struct algn_pos *ap1;

	max = algn->max_pos;
	int* proc = new int[slen];
	for (j=0; j<(int)slen; j++)
		proc[j] = 0;
	unsigned int chars[6]; //store count for -,a,c,g,t,n
	for (j=0; j<(int)max; j++) {
		for (s=0; s<6; s++)
			chars[s]=0;
		for(s=0;s<slen;s++) {
			sq = &(scol->seqs[s]);
			if(proc[s] < sq->length) {
				ap1 = find_eqc(ap,s,proc[s]);
				if(*ap1->eqcAlgnPos==j) {
					char cur_char = toupper(sq->data[proc[s]]);
					switch (cur_char) {
					case 'A':
						chars[1]++; break;
					case 'C':
						chars[2]++; break;
					case 'G':
						chars[3]++; break;
					case 'T':
						chars[4]++; break;
					case 'N':
						chars[5]++; break;
					default: //anything else is regarded as 'n'
						chars[5]++; break;
					}
					proc[s]++;
				} else {
					chars[0]++;
				}
			} else {
				chars[0]++;
			}
		}
		char c = make_consensus(chars);
		consensus += c;
		if (para->DEBUG > 5) {
			for (s=0; s<6; s++) printf("chars[%u]:%u; ", s, chars[s]);
			printf("\nconsensus: %c\n", c);
		}
	}
	delete[] proc;
}
