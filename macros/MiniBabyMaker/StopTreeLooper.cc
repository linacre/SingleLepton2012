#include "../../CORE/jetSmearingTools.h"

#include "StopTreeLooper.h"
#include "../Plotting/PlotUtilities.h"
#include "../Core/MT2Utility.h"
#include "../Core/mt2bl_bisect.h"
#include "../Core/mt2w_bisect.h"

#include "../Core/stopUtils.h"

#include "TROOT.h"
#include "TH1F.h"
#include "TH2F.h"
#include "TChainElement.h"
#include "TFile.h"
#include "TMath.h"
#include "TChain.h"
#include "Riostream.h"
#include "TFitter.h"

#include <algorithm>
#include <utility>
#include <map>
#include <set>
#include <list>

StopTreeLooper::StopTreeLooper()
{
  m_outfilename_ = "histos.root";
  // t1metphicorr = -9999.;
  // t1metphicorrphi = -9999.;
  // t1metphicorrmt = -9999.;
  // min_mtpeak = -9999.;
  // max_mtpeak = -9999.; 
}

StopTreeLooper::~StopTreeLooper()
{
}

void StopTreeLooper::setOutFileName(string filename)
{
  m_outfilename_ = filename;

}
//--------------------------------------------------------------------

struct DorkyEventIdentifier {
  // this is a workaround for not having unique event id's in MC
  unsigned long int run, event,lumi;
  bool operator < (const DorkyEventIdentifier &) const;
  bool operator == (const DorkyEventIdentifier &) const;
};

//--------------------------------------------------------------------

bool DorkyEventIdentifier::operator < (const DorkyEventIdentifier &other) const
{
  if (run != other.run)
    return run < other.run;
  if (event != other.event)
    return event < other.event;
  if(lumi != other.lumi)
    return lumi < other.lumi;
  return false;
}

//--------------------------------------------------------------------

bool DorkyEventIdentifier::operator == (const DorkyEventIdentifier &other) const
{
  if (run != other.run)
    return false;
  if (event != other.event)
    return false;
  return true;
}

//--------------------------------------------------------------------

std::set<DorkyEventIdentifier> already_seen; 
bool is_duplicate (const DorkyEventIdentifier &id) {
  std::pair<std::set<DorkyEventIdentifier>::const_iterator, bool> ret =
    already_seen.insert(id);
  return !ret.second;
}

//--------------------------------------------------------------------

std::set<DorkyEventIdentifier> events_lasercalib; 
int load_badlaserevents  () {

  ifstream in;
  in.open("../Core/badlaser_events.txt");

  int run, event, lumi;
  int nlines = 0;

  while (1) {
    in >> run >> event >> lumi;
    if (!in.good()) break;
    nlines++;
    DorkyEventIdentifier id = {run, event, lumi };
    events_lasercalib.insert(id);
  }
  printf(" found %d bad events \n",nlines);

  in.close();

  return 0;

}

bool is_badLaserEvent (const DorkyEventIdentifier &id) {
  if (events_lasercalib.find(id) != events_lasercalib.end()) return true;
  return false;
}



bool compare_candidates( Candidate x, Candidate y ){
  return x.chi2 < y.chi2;
}

double fc2 (double c1, double m12, double m22, double m02, bool verbose = false)
{
  if (verbose) {
    printf("c1: %4.2f\n", c1);
    printf("m12: %4.2f\n", m12);
    printf("m22: %4.2f\n", m22);
    printf("m02: %4.2f\n", m02);
  }

  double a = m22;
  double b = (m02 - m12 - m22) * c1;
  double c = m12 * c1 * c1 - PDG_W_MASS * PDG_W_MASS;

  if (verbose) {
    printf("a: %4.2f\n", a);
    printf("b: %4.2f\n", b);
    printf("c: %4.2f\n", c);
  }

  double num = -1. * b + sqrt(b * b - 4 * a * c);
  double den = 2 * a;

  if (verbose) {
    printf("num: %4.2f\n", num);
    printf("den: %4.2f\n", den);
    printf("num/den: %4.2f\n", num/den);
  }

  return (num/den);
}


double fchi2 (double c1, double pt1, double sigma1, double pt2, double sigma2,
              double m12, double m22, double m02){
  double rat1 = pt1 * (1 - c1) / sigma1;
  double rat2 = pt2 * (1 - fc2(c1, m12, m22, m02)) / sigma2;

  return ( rat1 * rat1 + rat2 * rat2);
}

//void StopSelector::minuitFunction(int& npar, double *gout, double &result, double par[], int flg)
void minuitFunction(int&, double* , double &result, double par[], int){
  result=fchi2(par[0], par[1], par[2], par[3], par[4], par[5], par[6], par[7]);
}

float getDataMCRatio(float eta){
  if (eta >=0.0 && eta < 0.5) return 1.052;
  if (eta >=0.5 && eta < 1.1) return 1.057;
  if (eta >=1.1 && eta < 1.7) return 1.096;
  if (eta >=1.7 && eta < 2.3) return 1.134;
  if (eta >=2.3 && eta < 5.0) return 1.288;
  return 1.0;
}


/* Reconstruct the hadronic top candidates, select the best candidate and
 * store the chi2 =  (m_jj - m_W)^2/sigma_m_jj + (m_jjj - m_t)^2/sigma_m_jjj
 * return the number of candidates found.
 *
 * n_jets - number of jets.
 * jets - jets
 * btag - b-tagging information of the jets
 * mc - qgjet montecarlo match number for the jets
 * 
 * returns a list of candidates sorted by chi2 ( if __sort = true in .h );
 */ 

list<Candidate> StopTreeLooper::recoHadronicTop(StopTree* tree, bool isData, bool wbtag){

  assert( tree->pfjets_->size() == tree->pfjets_csv_.size() );

  static JetSmearer* jetSmearer = 0;
  if (jetSmearer == 0 ){
    std::vector<std::string> list_of_file_names;
    list_of_file_names.push_back("../../CORE/jetsmear/data/Spring10_PtResolution_AK5PF.txt");
    list_of_file_names.push_back("../../CORE/jetsmear/data/Spring10_PhiResolution_AK5PF.txt");
    list_of_file_names.push_back("../../CORE/jetsmear/data/jet_resolutions.txt");
    jetSmearer = makeJetSmearer(list_of_file_names);
  }

  LorentzVector* lep = &tree->lep1_;
  double met = tree->t1metphicorr_;
  double metphi = tree->t1metphicorrphi_;

  vector<LorentzVector> jets;
  vector<float>         btag;
  vector<int>           mc;

  //cout << endl << "baby branches:" << endl;
  for( unsigned int i = 0 ; i < tree->pfjets_->size() ; ++i ){
    //cout << i << " pt csv " << tree->pfjets_->at(i).pt() << " " << tree->pfjets_csv_.at(i) << endl;
    jets.push_back( tree->pfjets_->at(i)    );
    btag.push_back( tree->pfjets_csv_.at(i) );
    if ( !isData ) mc.push_back  ( tree->pfjets_mc3_.at(i) );
    else mc.push_back  ( 0 );
  } 

  // cout << endl << "stored:" << endl;
  // for( unsigned int i = 0 ; i < jets.size() ; ++i ){
  //   cout << i << " pt csv " << jets.at(i).pt() << " " << btag.at(i) << endl;
  // } 

  assert( jets.size() == btag.size() );

  float metx = met * cos( metphi );
  float mety = met * sin( metphi );

  int n_jets = jets.size();
  double sigma_jets[n_jets];
  for (int i=0; i<n_jets; ++i)
    sigma_jets[i] = getJetResolution(jets[i], jetSmearer);

  if ( isData )
    for (int i=0; i<n_jets; ++i)
      sigma_jets[i] *= getDataMCRatio(jets[i].eta());


  int ibl[5];
  int iw1[5];
  int iw2[5];
  int ib[5];

  if ( !isData ){
     
    // Matching MC algoritm search over all conbinations  until the 
    // right combination is found. More than one candidate is suported 
    //  but later only the first is used.
    // 
    int match = 0;
    for (int jbl=0; jbl<n_jets; ++jbl )
      for (int jb=0; jb<n_jets; ++jb )
        for (int jw1=0; jw1<n_jets; ++jw1 )
          for (int jw2=jw1+1; jw2<n_jets; ++jw2 )
            if ( (mc.at(jw2)==2 && mc.at(jw1)==2 && mc.at(jb)==1 && mc.at(jbl)==-1) ||
                 (mc.at(jw2)==-2 && mc.at(jw1)==-2 && mc.at(jb)==-1 && mc.at(jbl)==1) ) {
	      ibl[match] = jbl;
	      iw1[match] = jw1;
	      iw2[match] = jw2;
	      ib[match] = jb;
	      match++;
            }
  }

  
  ////////    * Combinatorics. j_1 Pt must be > PTMIN_W1 and so on.
  
  vector<int> v_i, v_j;
  vector<double> v_k1, v_k2;
  for ( int i=0; i<n_jets; ++i )
    for ( int j=i+1; j<n_jets; ++j ){
      double pt_w1 = jets[i].Pt();
      double pt_w2 = jets[j].Pt();
      if (pt_w1 < PTMIN_J1  || pt_w2 < PTMIN_J2)
        continue;

      //
      //  W
      //
      LorentzVector hadW = jets[i] + jets[j];

      //
      //  W Mass Constraint.
      //
      TFitter *minimizer = new TFitter();
      double p1 = -1;

      minimizer->ExecuteCommand("SET PRINTOUT", &p1, 1);
      minimizer->SetFCN(minuitFunction);
      minimizer->SetParameter(0 , "c1"     , 1.1             , 1 , 0 , 0);
      minimizer->SetParameter(1 , "pt1"    , 1.0             , 1 , 0 , 0);
      minimizer->SetParameter(2 , "sigma1" , sigma_jets[i]   , 1 , 0 , 0);
      minimizer->SetParameter(3 , "pt2"    , 1.0             , 1 , 0 , 0);
      minimizer->SetParameter(4 , "sigma2" , sigma_jets[j]   , 1 , 0 , 0);
      minimizer->SetParameter(5 , "m12"    , jets[i].mass2() , 1 , 0 , 0);
      minimizer->SetParameter(6 , "m22"    , jets[j].mass2() , 1 , 0 , 0);
      minimizer->SetParameter(7 , "m02"    , hadW.mass2()    , 1 , 0 , 0);

      for (unsigned int k = 1; k < 8; k++)
        minimizer->FixParameter(k);

      minimizer->ExecuteCommand("SIMPLEX", 0, 0);
      minimizer->ExecuteCommand("MIGRAD", 0, 0);

      double c1 = minimizer->GetParameter(0);
      double c2 = fc2(c1, jets[i].mass2(), jets[j].mass2(), hadW.mass2());
                
      delete minimizer;

     
      //     * W Mass check :)
      //     *  Never trust a computer you can't throw out a window. 
      //      *  - Steve Wozniak 

      //      cout << "c1 = " <<  c1 << "  c1 = " << c2 << "   M_jj = " 
      //           << ((jets[i] * c1) + (jets[j] * c2)).mass() << endl;
      
      v_i.push_back(i);
      v_j.push_back(j);
      v_k1.push_back(c1);
      v_k2.push_back(c2);
    }


  list<Candidate> chi2candidates;
        
  mt2_bisect::mt2 mt2_event;
  mt2bl_bisect::mt2bl mt2bl_event;
  mt2w_bisect::mt2w mt2w_event;
  
  for ( int b=0; b<n_jets; ++b )
    for (int o=0; o<n_jets; ++o){
      if ( b == o )
        continue;

      if ( wbtag && btag[b] < BTAG_MIN && btag[o] < BTAG_MIN )
        continue;

      double pt_b = jets[b].Pt();
      if ( wbtag && btag[b] >= BTAG_MIN && pt_b < PTMIN_BTAG )
        continue;

      if ( btag[b] < BTAG_MIN && pt_b < PTMIN_B )
        continue;

      double pt_o = jets[o].Pt();
      if ( wbtag && btag[o] >= BTAG_MIN && pt_o < PTMIN_OTAG )
        continue;

      if ( wbtag && btag[o] < BTAG_MIN && pt_o < PTMIN_O)
        continue;

      ///
      //  MT2 Variables
      ///

      double pl[4];     // Visible lepton
      double pb1[4];    // bottom on the same side as the visible lepton
      double pb2[4];    // other bottom, paired with the invisible W
      double pmiss[3];  // <unused>, pmx, pmy   missing pT
      pl[0]= lep->E(); pl[1]= lep->Px(); pl[2]= lep->Py(); pl[3]= lep->Pz();
      pb1[1] = jets[o].Px();  pb1[2] = jets[o].Py();   pb1[3] = jets[o].Pz();
      pb2[1] = jets[b].Px();  pb2[2] = jets[b].Py();   pb2[3] = jets[b].Pz();
      pmiss[0] = 0.; pmiss[1] = metx; pmiss[2] = mety;

      double pmiss_lep[3];
      pmiss_lep[0] = 0.;
      pmiss_lep[1] = pmiss[1]+pl[1]; pmiss_lep[2] = pmiss[2]+pl[2];

      pb1[0] = jets[o].mass();
      pb2[0] = jets[b].mass();
      mt2_event.set_momenta( pb1, pb2, pmiss_lep );
      mt2_event.set_mn( 80.385 );   // Invisible particle mass
      double c_mt2b = mt2_event.get_mt2();

      pb1[0] = jets[o].E();
      pb2[0] = jets[b].E();
      mt2bl_event.set_momenta(pl, pb1, pb2, pmiss);
      double c_mt2bl = mt2bl_event.get_mt2bl();

      mt2w_event.set_momenta(pl, pb1, pb2, pmiss);
      double c_mt2w = mt2w_event.get_mt2w();

      //      cout << b << ":"<< btag[b] << " - " << o << ":" << btag[o] << " = " << c_mt2w << endl;

      for (unsigned int w = 0; w < v_i.size() ; ++w ){
        int i = v_i[w];
        int j = v_j[w];
        if ( i==o || i==b || j==o || j==b )
	  continue;

        double pt_w1 = jets[i].Pt();
        double pt_w2 = jets[j].Pt();

	///
	//  W Mass.
	///
	LorentzVector hadW = jets[i] + jets[j];
	double massW = hadW.mass();

	double c1 = v_k1[w];
	double c2 = v_k2[w];

	///
	// Top Mass.
	///
        LorentzVector hadT = (jets[i] * c1) + (jets[j] * c2) + jets[b];
        double massT = hadT.mass();

        double pt_w = hadW.Pt();
        double sigma_w2 = pt_w1*sigma_jets[i] * pt_w1*sigma_jets[i]
	  + pt_w2*sigma_jets[j] * pt_w2*sigma_jets[j];
        double smw2 = (1.+2.*pt_w*pt_w/massW/massW)*sigma_w2;
        double pt_t = hadT.Pt();
        double sigma_t2 = c1*pt_w1*sigma_jets[i] * c1*pt_w1*sigma_jets[i]
	  + c2*pt_w2*sigma_jets[j] * c2*pt_w2*sigma_jets[j]
	  + pt_b*sigma_jets[b] * pt_b*sigma_jets[b];
        double smtop2 = (1.+2.*pt_t*pt_t/massT/massT)*sigma_t2;

        double c_chi2 = (massT-PDG_TOP_MASS)*(massT-PDG_TOP_MASS)/smtop2
	  + (massW-PDG_W_MASS)*(massW-PDG_W_MASS)/smw2;

        bool c_match = ( !isData &&  iw1[0]==i && iw2[0]==j && ib[0]==b && ibl[0]==o );
  
        Candidate c;
        c.chi2  = c_chi2;
        c.mt2b  = c_mt2b;
        c.mt2w  = c_mt2w;
        c.mt2bl = c_mt2bl;
        c.j1 = i;
        c.j2 = j;
        c.bi = b;
        c.oi = o;
        c.k1 = c1;
        c.k2 = c2;
        c.match = c_match;

        chi2candidates.push_back(c);
      }
    }

  if (__SORT) 
    chi2candidates.sort(compare_candidates);

  return chi2candidates;
}

//--------------------------------------------------------------------

// removes candidates without b-tagging requirement
list<Candidate> StopTreeLooper::getBTaggedCands(list<Candidate> &candidates, StopTree* tree) 
{

  list<Candidate> bcands; 

  list<Candidate>::iterator candIter;

  for(candIter = candidates.begin() ; candIter != candidates.end() ; candIter++ ){

    if( tree->pfjets_csv_.at(candIter->bi) < BTAG_MIN && tree->pfjets_csv_.at(candIter->oi) < BTAG_MIN ) continue;
    bcands.push_back(*candIter);

  }

  return bcands;

}

//--------------------------------------------------------------------

MT2struct StopTreeLooper::Best_MT2Calculator_Ricardo(list<Candidate> candidates, StopTree* tree, bool isData){

  if (candidates.size() == 0){
    MT2struct mfail;
    mfail.mt2w  = -0.999;
    mfail.mt2b  = -0.999;
    mfail.mt2bl = -0.999;
    mfail.chi2  = -0.999;
    return mfail;
  }

  double chi2_min  = 9999;
  double mt2b_min  = 9999;
  double mt2bl_min = 9999;
  double mt2w_min  = 9999;

  // count btags among leading 4 jets
  int n_btag = 0;

  for( int i = 0 ; i < 4 ; i++ ){
    if( tree->pfjets_csv_.at(i) < 0.679 ) n_btag++;
  }

  list<Candidate>::iterator candIter;

  for(candIter = candidates.begin() ; candIter != candidates.end() ; candIter++ ){

    // loop over all candidiates
    //for (unsigned int i=0; i<candidates.size(); ++i) {

    //if ( ! candidates_->at(i).match ) continue;

    // get indices of 4 jets used for hadronic top reconstruction
    // int b  = candidates.at(i).bi;
    // int o  = candidates.at(i).oi;
    // int j1 = candidates.at(i).j1;
    // int j2 = candidates.at(i).j2;

    int b  = (*candIter).bi;
    int o  = (*candIter).oi;
    int j1 = (*candIter).j1;
    int j2 = (*candIter).j2;

    // require the 4 jets used for the hadronic top mass are the 4 leading jets
    if (b>3 || o > 3 || j1 > 3 || j2 > 3) continue;

    // store whether the 2 jets used for MT2 calculation are btagged
    bool b_btag  = (tree->pfjets_csv_.at(b)  > 0.679);
    bool o_btag  = (tree->pfjets_csv_.at(o)  > 0.679);
    //bool j1_btag = (btag_->at(j1) > 0.679);
    //bool j2_btag = (btag_->at(j2) > 0.679);

    // 2 btags: require jets used for MT2 are the 2 b-jets
    if ( n_btag == 2 ){ 
      if ( !b_btag || !o_btag ) continue;
    } 

    // 1 btag: require the bjet and one of the 2 leading non bjets is used for MT2
    else if ( n_btag == 1) {

      if( b_btag ){
	if( b == 3  && o > 1 ) continue;
	if( b <   3 && o > 2 ) continue;
      }

      if( o_btag ){
	if( o == 3  && b > 1 ) continue;
	if( o <   3 && b > 2 ) continue;
      }

      //if (b>1 || o>1) continue;
    } 

    // 0 or >=3 btags: require jets used for MT2 are among 3 leading jets
    else {
      if (b>2 || o>2) continue;
    }

    // double chi2  = candidates.at(i).chi2;
    // double mt2b  = candidates.at(i).mt2b;
    // double mt2bl = candidates.at(i).mt2bl;
    // double mt2w  = candidates.at(i).mt2w;

    double chi2  = (*candIter).chi2;
    double mt2b  = (*candIter).mt2b;
    double mt2bl = (*candIter).mt2bl;
    double mt2w  = (*candIter).mt2w;
 
    //    cout << " " << b << ":" << b_btag  << " " << o << ":" << o_btag << " " << j1 << " " << j2 << " = " << mt2w  << "  " << chi2 <<endl;
    if (chi2  < chi2_min  ) chi2_min  = chi2;
    if (mt2b  < mt2b_min  ) mt2b_min  = mt2b;
    if (mt2bl < mt2bl_min ) mt2bl_min = mt2bl;
    if (mt2w  < mt2w_min  ) mt2w_min  = mt2w;
  }

  if( mt2w_min  > 9000 ) mt2w_min  = -0.999;
  if( mt2b_min  > 9000 ) mt2b_min  = -0.999;
  if( mt2bl_min > 9000 ) mt2bl_min = -0.999;
  if( chi2_min  > 9000 ) chi2_min  = -0.999;

  MT2struct m;
  m.mt2w  = mt2w_min;
  m.mt2b  = mt2b_min;
  m.mt2bl = mt2bl_min;
  m.chi2  = chi2_min;

  return m;

}

void StopTreeLooper::loop(TChain *chain, TString name)
{

  printf("[StopTreeLooper::loop] %s\n", name.Data());

  load_badlaserevents  ();

  //---------------------------------
  // check for valid chain
  //---------------------------------

  TObjArray *listOfFiles = chain->GetListOfFiles();
  TIter fileIter(listOfFiles);
  if (listOfFiles->GetEntries() == 0) {
    cout << "[StopTreeLooper::loop] no files in chain" << endl;
    return;
  }

  //---------------------------------
  // set up histograms
  //---------------------------------

  gROOT->cd();

  cout << "[StopTreeLooper::loop] setting up histos" << endl;

  //plotting map
  std::map<std::string, TH1F*> h_1d;//h_cr1, h_cr4, h_cr5;

  makeTree(name.Data());

  // TFile* vtx_file = TFile::Open("vtxreweight/vtxreweight_Summer12_DR53X-PU_S10_9p7ifb_Zselection.root");
  // if( vtx_file == 0 ){
  //   cout << "vtxreweight error, couldn't open vtx file. Quitting!"<< endl;
  //   exit(0);
  // }

  // TH1F* h_vtx_wgt = (TH1F*)vtx_file->Get("hratio");
  // h_vtx_wgt->SetName("h_vtx_wgt");

  //
  // file loop
  //

  unsigned int nEventsChain=0;
  unsigned int nEvents = chain->GetEntries();
  nEventsChain = nEvents;
  unsigned int nEventsTotal = 0;
  int i_permille_old = 0;

  bool isData = name.Contains("data") ? true : false;

  cout << "[StopTreeLooper::loop] running over chain with total entries " << nEvents << endl;

  while (TChainElement *currentFile = (TChainElement*)fileIter.Next()) {

    //---------------------------------
    // load the stop baby tree
    //---------------------------------

    StopTree *tree = new StopTree();
    tree->LoadTree(currentFile->GetTitle());
    tree->InitTree();

    //---------------------------------
    // event loop
    //---------------------------------

    ULong64_t nEvents = tree->tree_->GetEntries();
    nEvents = 1000;

    for(ULong64_t event = 0; event < nEvents; ++event) {
      tree->tree_->GetEntry(event);

      //---------------------------------
      // increment counters
      //---------------------------------

      ++nEventsTotal;
      if (nEventsTotal%10000==0) {
	int i_permille = (int)floor(1000 * nEventsTotal / float(nEventsChain));
	//if (i_permille != i_permille_old) {//this prints too often!
	// xterm magic from L. Vacavant and A. Cerri
	if (isatty(1)) {
	  printf("\015\033[32m ---> \033[1m\033[31m%4.1f%%"
		 "\033[0m\033[32m <---\033[0m\015", i_permille/10.);
	  fflush(stdout);
	}
	i_permille_old = i_permille;
      }

      //------------------------------------------ 
      // skip duplicates
      //------------------------------------------ 

      if( isData ) {
        DorkyEventIdentifier id = {tree->run_,tree->event_, tree->lumi_ };
        if (is_duplicate(id) ){
          continue;
        }
	if (is_badLaserEvent(id) ){
	  //std::cout<<"Removed bad laser calibration event:" <<tree->run_<<"   "<<tree->event_<<"\n";
	  continue;
	}
      }

      //------------------------------------------ 
      // event weight
      //------------------------------------------ 

      float evtweight    = isData ? 1. : ( tree->weight_ * tree->nvtxweight_ * tree->mgcor_ );
      float trigweight   = isData ? 1. : getsltrigweight(tree->id1_, tree->lep1_.Pt(), tree->lep1_.Eta());
      float trigweightdl = isData ? 1. : getdltrigweight(tree->id1_, tree->id2_);

      //------------------------------------------ 
      // selection criteria
      //------------------------------------------ 
      
      if ( !passEvtSelection(tree, name) ) continue; // >=1 lepton, rho cut, MET filters, veto 2 nearby leptons
      if ( tree->npfjets30_ < 4          ) continue; // >=4 jets
      if ( tree->t1metphicorr_ < 50.0    ) continue; // MET > 50 GeV

      //------------------------------------------ 
      // variables to add to baby
      //------------------------------------------ 
      
      initBaby(); // set all branches to -1

      list<Candidate> allcandidates = recoHadronicTop(tree, isData , false);
      MT2struct mr                  = Best_MT2Calculator_Ricardo(allcandidates, tree, isData);

      // which selections are passed
      sig_        = ( passOneLeptonSelection(tree, isData) && tree->nbtagscsvm_>=1 ) ? 1 : 0;
      cr1_        = ( passOneLeptonSelection(tree, isData) && tree->nbtagscsvm_==0 ) ? 1 : 0;
      cr4_        = ( passDileptonSelection(tree, isData) )                          ? 1 : 0;
      cr5_        = ( passLepPlusIsoTrkSelection(tree, isData) )                     ? 1 : 0;

      // kinematic variables
      met_        = tree->t1metphicorr_;
      mt_         = tree->t1metphicorrmt_;
      chi2_       = mr.chi2;
      mt2w_       = mr.mt2w;
      mt2b_       = mr.mt2b;
      mt2bl_      = mr.mt2bl;

      // weights
      weight_     = evtweight;
      sltrigeff_  = trigweight;
      dltrigeff_  = trigweightdl;

      // hadronic variables
      nb_         = tree->nbtagscsvm_;
      njets_      = tree->npfjets30_;

      // lepton variables
      passisotrk_ = passIsoTrkVeto(tree) ? 1 : 0;
      nlep_       = tree->ngoodlep_;

      lep1pt_     = tree->lep1_.pt();
      lep1eta_    = tree->lep1_.eta();

      if( nlep_ > 1 ){
	lep2pt_    = tree->lep1_.pt();
	lep2eta_   = tree->lep1_.eta();
	dilmass_   = tree->dilmass_;
      }
      
      // fill me up
      outTree_->Fill();

    } // end event loop
  } // end file loop
  
  //-------------------------
  // finish and clean up
  //-------------------------

  outFile_->cd();
  outTree_->Write();
  outFile_->Close();
  delete outFile_;

  already_seen.clear();

  gROOT->cd();

}

//--------------------------------------------
// create the tree and set branch addresses
//--------------------------------------------

void StopTreeLooper::makeTree(const char *prefix){

  TDirectory *rootdir = gDirectory->GetDirectory("Rint:");
  rootdir->cd();

  outFile_   = new TFile(Form("output/%s_mini.root",prefix), "RECREATE");
  outFile_->cd();

  outTree_ = new TTree("t","Tree");

  outTree_->Branch("lep1pt"       ,        &lep1pt_      ,         "lep1pt/F"		);
  outTree_->Branch("lep1eta"      ,        &lep1eta_     ,         "lep1eta/F"		);
  outTree_->Branch("sig"          ,        &sig_         ,         "sig/I"		);
  outTree_->Branch("cr1"          ,        &cr1_         ,         "cr1/I"		);
  outTree_->Branch("cr4"          ,        &cr4_         ,         "cr4/I"		);
  outTree_->Branch("cr5"          ,        &cr5_         ,         "cr5/I"		);
  outTree_->Branch("met"          ,        &met_         ,         "met/F"		);
  outTree_->Branch("mt"           ,        &mt_          ,         "mt/F"		);
  outTree_->Branch("chi2"         ,        &chi2_        ,         "chi2/F"		);
  outTree_->Branch("mt2w"         ,        &mt2w_        ,         "mt2w/F"		);
  outTree_->Branch("mt2b"         ,        &mt2b_        ,         "mt2b/F"		);
  outTree_->Branch("mt2bl"        ,        &mt2bl_       ,         "mt2bl/F"		);
  outTree_->Branch("weight"       ,        &weight_      ,         "weight/F"		);
  outTree_->Branch("sltrigeff"    ,        &sltrigeff_   ,         "sltrigeff/F"	);
  outTree_->Branch("dltrigeff"    ,        &dltrigeff_   ,         "dltrigeff/F"	);
  outTree_->Branch("nb"           ,        &nb_          ,         "nb/I"		);
  outTree_->Branch("njets"        ,        &njets_       ,         "njets/I"		);
  outTree_->Branch("passisotrk"   ,        &passisotrk_  ,         "passisotrk/I"	);
  outTree_->Branch("nlep"         ,        &nlep_        ,         "nlep/I"		);
  outTree_->Branch("lep1pt"       ,        &lep1pt_      ,         "lep1pt/F"		);
  outTree_->Branch("lep1eta"      ,        &lep1eta_     ,         "lep1eta/F"		);
  outTree_->Branch("lep2pt"       ,        &lep2pt_      ,         "lep2pt/F"		);
  outTree_->Branch("lep2eta"      ,        &lep2eta_     ,         "lep2eta/F"		);
  outTree_->Branch("dilmass"      ,        &dilmass_     ,         "dilmass/F"		);


}

//--------------------------------------------
// set all branches to -1
//--------------------------------------------

void StopTreeLooper::initBaby(){

  // which selections are passed
  sig_        = -1;
  cr1_        = -1;
  cr4_        = -1;
  cr5_        = -1; 

  // kinematic variables
  met_        = -1.0;
  mt_         = -1.0;
  chi2_       = -1.0;
  mt2w_       = -1.0;
  mt2b_       = -1.0;
  mt2bl_      = -1.0;

  // weights
  weight_     = -1.0;
  sltrigeff_  = -1.0;
  dltrigeff_  = -1.0;

  // hadronic variables
  nb_         = -1;
  njets_      = -1;

  // lepton variables
  passisotrk_ = -1;
  nlep_       = -1;
  lep1pt_     = -1.0;
  lep1eta_    = -1.0;
  lep2pt_     = -1.0;
  lep2eta_    = -1.0;
  dilmass_    = -1.0;

}
