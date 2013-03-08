#ifndef WHLOOPER_H
#define WHLOOPER_H

#include "TChain.h"
#include "TFile.h"
#include "TString.h"
#include "TH1F.h"
#include "TH2F.h"

#include <iostream>
#include "Math/LorentzVector.h"
 
#include <cmath>
#include <map>

using namespace std;

class WHLooper {

 public:
  typedef ROOT::Math::LorentzVector<ROOT::Math::PxPyPzE4D<float> > LorentzVector;

  enum csvpoint { CSVL, CSVM, CSVT };

  WHLooper();
  ~WHLooper();

  void setOutFileName(string filename); 
  void loop(TChain *chain, TString name);

 private:

  void fillHists1DWrapper(std::map<std::string, TH1F*>& h_1d, const float evtweight = 1., const std::string& dir = "");
  void fillHists1D(std::map<std::string, TH1F*>& h_1d, const float evtweight = 1., const std::string& dir = "", const std::string& suffix = "");

  float getCSVCut(const csvpoint csv = WHLooper::CSVM);

  string m_outfilename_;
  TFile* outfile_;
  //for phi corrected met
  float t1metphicorr;
  float t1metphicorrphi;
  float t1metphicorrmt;

  // internal vars to store
  std::vector<LorentzVector> jets_;
  std::vector<LorentzVector> bjets_;
  std::vector<float> jets_csv_;
  std::vector<int> jets_idx_;
  std::vector<int> bjets_idx_;
  int njets_;
  int njetsalleta_;
  int nbjets_;
  Float_t mt2b_;
  Float_t mt2bl_;
  Float_t mt2w_;

  // internal flags
  bool isWjets_;
  bool isTChiwh_;


};

#endif
