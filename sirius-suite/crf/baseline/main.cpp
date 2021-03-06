#include <stdio.h>
#include <fstream>
#include <map>
#include <list>
#include <set>
#include <iomanip>
#include <iostream>
#include <cfloat>
#include <sstream>
#include "crf.h"
#include "common.h"

#include "../../utils/timer.h"

using namespace std;

bool PERFORM_TOKENIZATION = false;
bool OUTPUT_TAG_PROBS = false;
bool STANDOFF = false;
bool UIMA = false;
bool ENJU = false;
int NBEST = 0;
vector<vector<Token> > sentences;

string MODEL_DIR = ".";  // the default directory for saving the models

const double PROB_OUTPUT_THRESHOLD =
    0.001;  // suppress output of tags with a very low probability

void tokenize(const string &s, vector<Token> &vt,
              const bool use_upenn_tokenizer);

void crf_decode_lookahead(Sentence &s, CRF_Model &m,
                          vector<map<string, double> > &tagp);

void read_WordNet(const string &wordnetdir);

ParenConverter paren_converter;

void print_help() {
  cout << "Lookahead POS Tagger, a trainable part-of-speech tagger." << endl;
  cout << "Usage: lapos [OPTION]... [FILE]" << endl;
  cout << "Annotate each word in FILE with a part-of-speech tag." << endl;
  cout << "By default, the tagger assumes that FILE contains one sentence per "
          "line" << endl;
  cout << "and the words are tokenized with white spaces." << endl;
  cout << "Use -t option if you want to process untokenized sentences." << endl;
  cout << endl;
  cout << "Mandatory arguments to long options are mandatory for short options "
          "too." << endl;
  cout << "  -m, --model=DIR        specify the directory containing the models"
       << endl;
  cout << "  -h, --help             display this help and exit" << endl;
  cout << endl;
  cout << "With no FILE, or when FILE is -, read standard input." << endl;
  cout << endl;
  cout << "Report bugs to <tsuruoka@gmail.com>" << endl;
  exit(0);
}

struct TagProb {
  string tag;
  double prob;
  TagProb(const string &t_, const double &p_) : tag(t_), prob(p_) {}
  bool operator<(const TagProb &x) const { return prob > x.prob; }
};

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "[ERROR] Invalid arguments provided.\n\n");
    fprintf(stderr, "Usage: %s [MODEL] [INPUT DATA]\n\n", argv[0]);
    exit(0);
  }
  STATS_INIT("kernel", "conditional_random_fields");
  PRINT_STAT_STRING("abrv", "crf");
  string WORDNET_DIR = "";

  string ifilename;

  for (int i = 1; i < argc; i++) {
    string v = argv[i];
    if ((v == "-m" || v == "--model") && i < argc - 1) {
      MODEL_DIR = argv[i + 1];
      i++;
      continue;
    }
    if (v.substr(0, 8) == "--model=") {
      MODEL_DIR = v.substr(8);
      continue;
    }
    if (v == "-") {
      ifilename = "";
      continue;
    }
    if (v == "-h" || v == "--help") print_help();

    if (v[0] == '-') {
      cerr << "error: unknown option " << v << endl;
      cerr << "Try `stepp --help' for more information." << endl;
      exit(1);
    }
    ifilename = v;
  }

  istream *is(&std::cin);
  ifstream ifile;
  if (ifilename != "") {
    ifile.open(ifilename.c_str());
    if (!ifile) {
      cerr << "error: cannot open " << ifilename << endl;
      exit(1);
    }
    is = &ifile;
  }

  CRF_Model crfm;

  if (!crfm.load_from_file(argv[1], false)) exit(1);

  string line;
  int nlines = 0;
  while (getline(*is, line)) {
    nlines++;
    vector<Token> vt;
    tokenize(line, vt, PERFORM_TOKENIZATION);
    sentences.push_back(vt);
  }
  PRINT_STAT_INT("sentences", (int)sentences.size());

  tic();
  for (size_t s = 0; s < sentences.size(); ++s) {
    vector<Token> vt = sentences[s];
    if (vt.size() > 990) {
      cerr << "warning: the sentence is too long. it has been truncated."
           << endl;
      while (vt.size() > 990) vt.pop_back();
    }

    // convert parantheses
    vector<string> org_strs;
    for (vector<Token>::iterator i = vt.begin(); i != vt.end(); ++i) {
      org_strs.push_back(i->str);
      i->str = paren_converter.Ptb2Pos(i->str);
      i->prd = "?";
    }

    if (STANDOFF) cout << line << endl;
    if (vt.size() == 0) {
      cout << endl;
      continue;
    }

    // tag the words
    vector<map<string, double> > tagp0, tagp1;
    crf_decode_lookahead(vt, crfm, tagp0);
    if (false) {
      assert(0);
      exit(1);
    } else {
      for (vector<Token>::const_iterator i = vt.begin(); i != vt.end(); ++i) {
        map<string, double> dummy;
        tagp1.push_back(dummy);
      }
    }

    // merge the outputs (simple interpolation of probabilities)
    vector<map<string, double> > tagp;  // merged
    for (size_t k = 0; k < vt.size(); k++) {
      const map<string, double> &crf = tagp0[k];
      const map<string, double> &ef = tagp1[k];
      map<string, double> m, m2;  // merged

      double sum = 0;
      for (map<string, double>::const_iterator j = crf.begin(); j != crf.end();
           ++j) {
        m.insert(pair<string, double>(j->first, j->second));
        sum += j->second;
      }

      for (map<string, double>::const_iterator j = ef.begin(); j != ef.end();
           ++j) {
        sum += j->second;
        if (m.find(j->first) == m.end()) {
          m.insert(pair<string, double>(j->first, j->second));
        } else {
          m[j->first] += j->second;
        }
      }

      const double th = PROB_OUTPUT_THRESHOLD * sum;
      for (map<string, double>::iterator j = m.begin(); j != m.end(); ++j) {
        if (j->second >= th) m2.insert(*j);
      }
      double maxp = -1;
      string maxtag;
      for (map<string, double>::iterator j = m2.begin(); j != m2.end(); ++j) {
        const double p = j->second;
        if (p > maxp) {
          maxp = p;
          maxtag = j->first;
        }
      }
      tagp.push_back(m2);
      sentences[s][k].prd = maxtag;
    }
  }
  PRINT_STAT_DOUBLE("crf", toc());

#ifdef TESTING
  FILE *f = fopen("../input/crf_tag.baseline", "w");

  for (size_t i = 0; i < sentences.size(); i++) {
    vector<Token> vt = sentences[i];
    for (size_t i = 0; i < vt.size(); i++) {
      string s = vt[i].str;
      string p = vt[i].prd;
      if (i == 0)
        fprintf(f, "%s/%s", s.c_str(), p.c_str());
      else
        fprintf(f, " %s/%s", s.c_str(), p.c_str());
    }
    fprintf(f, "\n");
  }

  fclose(f);
#endif

  STATS_END();
}
