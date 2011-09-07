#include "GeneratorInterface/CascadeInterface/plugins/Cascade2Hadronizer.h"

#include "GeneratorInterface/Core/interface/RNDMEngineAccess.h"

#include "HepMC/GenEvent.h" 
#include "HepMC/PdfInfo.h"
#include "HepMC/PythiaWrapper6_2.h"  
#include "GeneratorInterface/CascadeInterface/plugins/CascadeWrapper.h"   

#include "HepMC/HEPEVT_Wrapper.h"
#include "HepMC/IO_HEPEVT.h"
#include "HepMC/IO_GenEvent.h"

#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "GeneratorInterface/Core/interface/FortranCallback.h"

HepMC::IO_HEPEVT hepevtio;

#include "HepPID/ParticleIDTranslations.hh"

//-- Pythia6 routines and functionalities to pass around Pythia6 params

#include "FWCore/ServiceRegistry/interface/Service.h"
#include "FWCore/Utilities/interface/RandomNumberGenerator.h"
#include "GeneratorInterface/Pythia6Interface/interface/Pythia6Service.h"
#include "GeneratorInterface/Pythia6Interface/interface/Pythia6Declarations.h"

using namespace edm;
using namespace std;

#define debug 1

CLHEP::HepRandomEngine* fRandomEngine;

extern "C" {
    
  double dcasrn_(int *idummy) {

    static int call = 0;

    edm::Service<edm::RandomNumberGenerator> rng;
    fRandomEngine = &(rng->getEngine());
    double rdm_nb = fRandomEngine->flat();     

    if(debug && ++call < 100) cout<<"dcasrn from c++, call: "<<call<<" , seed: "<<rng->mySeed()<<" random number: "<<rdm_nb<<endl;
    
    return rdm_nb;
  }
}
  
namespace gen {
  
  class Pythia6ServiceWithCallback : public Pythia6Service {
    
  public:
    Pythia6ServiceWithCallback(const edm::ParameterSet& pset) : Pythia6Service(pset) {}
    
  private:
    void upInit() {
      FortranCallback::getInstance()->fillHeader(); 
    }
    
    void upEvnt() {
      FortranCallback::getInstance()->fillEvent(); 
    }
    
    bool upVeto() { 
      bool veto = false;
      if (!hepeup_.nup) veto = true; //-- LHE Common Blocks
      return(veto);
    }

  };
  
  struct {
    int n, npad, k[5][pyjets_maxn];
    double p[5][pyjets_maxn], v[5][pyjets_maxn];
  } pyjets_local;
  
  Cascade2Hadronizer::Cascade2Hadronizer(edm::ParameterSet const& pset) 
    : BaseHadronizer(pset),
      fPy6Service(new Pythia6ServiceWithCallback(pset)), //-- this will store py6 parameters for further settings 
      
      //-- fRandomEngine(&getEngineReference()),

      //-- defined in GeneratorInterface/Core/src/RNDMEngineAccess.cc
      //-- CLHEP::HepRandomEngine& gen::getEngineReference()
      //-- { edm::Service<edm::RandomNumberGenerator> rng;
      //--  return rng->getEngine(); }

      fComEnergy(pset.getParameter<double>("comEnergy")),
      fCrossSection(pset.getUntrackedParameter<double>("crossSection",-1.)),
      fFilterEfficiency(pset.getUntrackedParameter<double>("filterEfficiency",-1.)),
      
      fMaxEventsToPrint(pset.getUntrackedParameter<int>("maxEventsToPrint",0)),
      fHepMCVerbosity(pset.getUntrackedParameter<bool>("pythiaHepMCVerbosity",false)),
      fPythiaListVerbosity(pset.getUntrackedParameter<int>("pythiaPylistVerbosity",0)),
      
      fDisplayPythiaBanner(pset.getUntrackedParameter<bool>("displayPythiaBanner",false)),
      fDisplayPythiaCards(pset.getUntrackedParameter<bool>("displayPythiaCards",false)){

    fParameters = pset.getParameter<ParameterSet>("Cascade2Parameters");

    fConvertToPDG = false;
    if(pset.exists("doPDGConvert"))
      fConvertToPDG = pset.getParameter<bool>("doPDGConvert");
    
    //-- silence Pythia6 banner printout, unless display requested
   
    if(!fDisplayPythiaBanner){
      if(!call_pygive("MSTU(12)=12345")){
	throw edm::Exception(edm::errors::Configuration,"PythiaError") 
	  <<" Pythia did not accept MSTU(12)=12345";
      }
    }
    
    //-- silence printouts from PYGIVE, unless display requested
    
    if(!fDisplayPythiaCards){
      if(!call_pygive("MSTU(13)=0")){
	throw edm::Exception(edm::errors::Configuration,"PythiaError") 
	  <<" Pythia did not accept MSTU(13)=0";
      }
    }

    //-- tmp stuff to deal with EvtGen corrupting pyjets
    flushTmpStorage();
  }

  Cascade2Hadronizer::~Cascade2Hadronizer(){
    if(fPy6Service != 0) delete fPy6Service;
  }
  
  void Cascade2Hadronizer::flushTmpStorage(){
    
    pyjets_local.n = 0 ;
    pyjets_local.npad = 0 ;
   
    for(int ip=0; ip<pyjets_maxn; ip++){
      for(int i=0; i<5; i++){
	pyjets_local.k[i][ip] = 0;
	pyjets_local.p[i][ip] = 0.;
	pyjets_local.v[i][ip] = 0.;
      }
    }
    return;
  }
  
  void Cascade2Hadronizer::fillTmpStorage(){

    pyjets_local.n = pyjets.n;
    pyjets_local.npad = pyjets.npad;

    for(int ip=0; ip<pyjets_maxn; ip++){
      for(int i=0; i<5; i++){
	pyjets_local.k[i][ip] = pyjets.k[i][ip];
	pyjets_local.p[i][ip] = pyjets.p[i][ip];
	pyjets_local.v[i][ip] = pyjets.v[i][ip];
      }
    }
    return ;
  }

  void Cascade2Hadronizer::finalizeEvent(){

    HepMC::PdfInfo pdf;

    //-- filling factorization "Q scale" now! pthat moved to binningValues()
             
    if(event()->signal_process_id() <= 0) event()->set_signal_process_id(pypars.msti[0]);
    if(event()->event_scale() <=0 )       event()->set_event_scale(pypars.pari[22]);
    if(event()->alphaQED() <= 0)          event()->set_alphaQED(pyint1.vint[56]);
    if(event()->alphaQCD() <= 0)          event()->set_alphaQCD(pyint1.vint[57]);
   
   //-- get pdf info directly from Pythia6 and set it up into HepMC::GenEvent
   //-- S. Mrenna: Prefer vint block
   
   if(pdf.id1() <= 0)      pdf.set_id1(pyint1.mint[14] == 21 ? 0 : pyint1.mint[14]);
   if(pdf.id2() <= 0)      pdf.set_id2(pyint1.mint[15] == 21 ? 0 : pyint1.mint[15]);
   if(pdf.x1() <= 0)       pdf.set_x1(pyint1.vint[40]);
   if(pdf.x2() <= 0)       pdf.set_x2(pyint1.vint[41]);
   if(pdf.pdf1() <= 0)     pdf.set_pdf1(pyint1.vint[38] / pyint1.vint[40]);
   if(pdf.pdf2() <= 0)     pdf.set_pdf2(pyint1.vint[39] / pyint1.vint[41]);
   if(pdf.scalePDF() <= 0) pdf.set_scalePDF(pyint1.vint[50]);   
 
   event()->set_pdf_info(pdf) ;

   //-- this is "standard" Py6 event weight (corresponds to PYINT1/VINT(97))
 
   event()->weights().push_back(pyint1.vint[96]);
 
   //-- this is event weight as 1./VINT(99) (PYINT1/VINT(99) is returned by the PYEVWT) 
 
   event()->weights().push_back(1./(pyint1.vint[98]));

   //-- now create the GenEventInfo product from the GenEvent and fill the missing pieces

   eventInfo().reset(new GenEventInfoProduct(event().get()));

   //-- in Pythia6 pthat is used to subdivide samples into different bins
   //-- in LHE mode the binning is done by the external ME generator
   //-- which is likely not pthat, so only filling it for Py6 internal mode

   eventInfo()->setBinningValues(vector<double>(1,pypars.pari[16]));
  
   //-- here we treat long-lived particles
  
   if (pydat1.mstj[21]==3 || pydat1.mstj[21]==4) imposeProperTime();

   //-- convert particle IDs Py6 -> PDG (if requested)
   
   if(fConvertToPDG){
     for(HepMC::GenEvent::particle_iterator part = event()->particles_begin(); 
	 part != event()->particles_end(); ++part){
       (*part)->set_pdg_id(HepPID::translatePythiatoPDT((*part)->pdg_id()));
     }
   }
   
   //-- service printouts, if requested
   if(fMaxEventsToPrint > 0){
     fMaxEventsToPrint--;
     if(fPythiaListVerbosity) call_pylist(fPythiaListVerbosity);
     if(fHepMCVerbosity){
        cout <<"Event process = "<<pypars.msti[0]<<endl<<"----------------------"<<endl;
       event()->print();
     }
   }
   
   return;
  }

  bool Cascade2Hadronizer::hadronize(){
    return false;
  }

  bool Cascade2Hadronizer::generatePartonsAndHadronize(){
    
    //-- grab Py6 instance
    Pythia6Service::InstanceWrapper guard(fPy6Service);
    
    FortranCallback::getInstance()->resetIterationsPerEvent();
    
    //-- skip event if py6 considers it bad
    if(pyint1.mint[50] != 0 ){
      event().reset();
      return false;
    }
    
    //-- generation of the event with CASCADE
    call_event(); 

    //-- pythia pyhepc routine converts common PYJETS in common HEPEVT
    call_pyhepc(1);

    //-- delete the created event from memory
    event().reset(hepevtio.read_next_event());
    
    //-- this is to deal with post-gen tools & residualDecay() that may reuse PYJETS
    flushTmpStorage();
    fillTmpStorage();
    
    return true;
  }

  bool Cascade2Hadronizer::decay(){
    return true;
  }
  
  bool Cascade2Hadronizer::residualDecay(){
    
    //-- grab Py6 instance
    Pythia6Service::InstanceWrapper guard(fPy6Service);
    
    int NPartsBeforeDecays = pyjets_local.n ;
    int NPartsAfterDecays = event().get()->particles_size();
    int barcode = NPartsAfterDecays;
      
    // JVY: well, in principle, it's not a 100% fair to go up to NPartsBeforeDecays,
    // because Photos will attach gamma's to existing vertices, i.e. in the middle
    // of the event rather than at the end; but this will only shift pointers down,
    // so we'll be going again over a few "original" particle...
    // in the alternative, we may go all the way up to the beginning of the event
    // and re-check if anything remains to decay, that's fine even if it'll take
    // some extra CPU...
    
    for(int ipart=NPartsAfterDecays; ipart>NPartsBeforeDecays; ipart-- ){
      
      HepMC::GenParticle* part = event().get()->barcode_to_particle( ipart );
      int status = part->status();
      if ( status != 1 ) continue; // check only "stable" particles, 
                                   // as some undecayed may still be marked as such
      int pdgid  = part->pdg_id();
      int py6ptr = pycomp_( pdgid );
      
      if(pydat3.mdcy[0][py6ptr-1] != 1) continue; // particle is not expected to decay
      
      int py6id = HepPID::translatePDTtoPythia(pdgid);
      
      // first, will need to zero out, then fill up PYJETS
      // I better do it directly (by hands) rather than via py1ent 
      // - to avoid (additional) mass smearing
      
      if(part->momentum().t() <= part->generated_mass()){
	continue ; // e == m -> 0-momentum, nothing to decay...
      }
    
      else{
	pyjets.n = 0;
	
	for(int i=0; i<5; i++){
	  pyjets.k[i][0] = 0;
	  pyjets.p[i][0] = 0.;
	  pyjets.v[i][0] = 0.;
	}
	
	pyjets.k[0][0] = 1;
	pyjets.k[1][0] = py6id;
	pyjets.p[4][0] = part->generated_mass();
	pyjets.p[3][0] = part->momentum().t();
	pyjets.p[0][0] = part->momentum().x();
	pyjets.p[1][0] = part->momentum().y();
	pyjets.p[2][0] = part->momentum().z();
	HepMC::GenVertex* prod_vtx = part->production_vertex();
	if ( !prod_vtx ) continue; // in principle, should never happen but...
	pyjets.v[0][0] = prod_vtx->position().x();
	pyjets.v[1][0] = prod_vtx->position().y();
	pyjets.v[2][0] = prod_vtx->position().z();
	pyjets.v[3][0] = prod_vtx->position().t();
	pyjets.v[4][0] = 0.;
	pyjets.n = 1;
	pyjets.npad = pyjets_local.npad;
      }
      
      //-- now call Py6 decay routine
     
      int parent = 1; // since we always pass to Py6 a single particle
      pydecy_(parent);
      
      //-- now attach decay products to mother
      
      for(int iprt1=1; iprt1<pyjets.n; iprt1++){

	part->set_status( 2 );
	
	HepMC::GenVertex* DecVtx = new HepMC::GenVertex( HepMC::FourVector(pyjets.v[0][iprt1],
									   pyjets.v[1][iprt1],
									   pyjets.v[2][iprt1],
									   pyjets.v[3][iprt1]) );
	DecVtx->add_particle_in( part ); // this will cleanup end_vertex if exists, replace with the new one
	// I presume (vtx) barcode will be given automatically
	
	HepMC::FourVector  pmom(pyjets.p[0][iprt1],pyjets.p[1][iprt1],
				pyjets.p[2][iprt1],pyjets.p[3][iprt1] );
	
	int dstatus = 0;
	if(pyjets.k[0][iprt1] >= 1 && pyjets.k[0][iprt1] <= 10) dstatus = 1;
	else if(pyjets.k[0][iprt1] >= 11 && pyjets.k[0][iprt1] <= 20) dstatus = 2;
	else if(pyjets.k[0][iprt1] >= 21 && pyjets.k[0][iprt1] <= 30) dstatus = 3;
	else if(pyjets.k[0][iprt1] >= 31 && pyjets.k[0][iprt1] <= 100) dstatus = pyjets.k[0][iprt1];
	
	HepMC::GenParticle* daughter = new HepMC::GenParticle(pmom,
							      HepPID::translatePythiatoPDT(pyjets.k[1][iprt1]),
							      dstatus);
	barcode++;
	daughter->suggest_barcode( barcode ); 
	DecVtx->add_particle_out( daughter );
	
	int iprt2;
	for(iprt2=iprt1+1; iprt2<pyjets.n; iprt2++){ // the pointer is shifted by -1, c++ style
	 
	  if(pyjets.k[2][iprt2] != parent){
	    parent = pyjets.k[2][iprt2];
	    break; // another parent particle; reset & break the loop
	  }
	       
	  HepMC::FourVector  pmomN(pyjets.p[0][iprt2],pyjets.p[1][iprt2],
				   pyjets.p[2][iprt2],pyjets.p[3][iprt2] );
	       
	  dstatus = 0;
	  if(pyjets.k[0][iprt2] >= 1 && pyjets.k[0][iprt2] <= 10) dstatus = 1;
	  else if(pyjets.k[0][iprt2] >= 11 && pyjets.k[0][iprt2] <= 20) dstatus = 2;
	  else if(pyjets.k[0][iprt2] >= 21 && pyjets.k[0][iprt2] <= 30) dstatus = 3;
	  else if(pyjets.k[0][iprt2] >= 31 && pyjets.k[0][iprt2] <= 100) dstatus = pyjets.k[0][iprt2];
	  
	  HepMC::GenParticle* daughterN = new HepMC::GenParticle(pmomN,
								 HepPID::translatePythiatoPDT( pyjets.k[1][iprt2] ),
								 dstatus);
	  barcode++;
	  daughterN->suggest_barcode( barcode ); 
	  DecVtx->add_particle_out( daughterN );	     
	} //-- end iprt2 loop
	 
	iprt1 = iprt2-1; // reset counter such that it doesn't go over the same child more than once
	// don't forget to offset back into c++ counting, as it's already +1 forward
	
	event().get()->add_vertex( DecVtx );
      } //-- end iprt1 loop
    } //-- end loop over decay products
   
    // now restore the very original Py6 event record 
  
    if(pyjets_local.n != pyjets.n){
   
      // restore pyjets to its state as it was before external decays -
      // might have been jammed by action above or by py1ent calls in EvtGen

      pyjets.n = pyjets_local.n;
      pyjets.npad = pyjets_local.npad;
      for(int ip=0; ip<pyjets_local.n; ip++){
	for(int i=0; i<5; i++){
	  pyjets.k[i][ip] = pyjets_local.k[i][ip];
	  pyjets.p[i][ip] = pyjets_local.p[i][ip];
	  pyjets.v[i][ip] = pyjets_local.v[i][ip];
	}
      }
    }  
         
   return true;
  }

  bool Cascade2Hadronizer::readSettings(int key) {

    //-- grab Py6 instance
    Pythia6Service::InstanceWrapper guard(fPy6Service); 
    
    fPy6Service->setGeneralParams();
    
    // if (key == 0) fPy6Service->setCSAParams();
    // fPy6Service->setSLHAParams();
    
    return true;
  }

  bool Cascade2Hadronizer::initializeForExternalPartons(){
    return false;
  }

  bool Cascade2Hadronizer::initializeForInternalPartons(){

    //-- grab Py6 instance
    Pythia6Service::InstanceWrapper guard(fPy6Service);
  
    //-- change standard parameters of JETSET/PYTHIA - replace call_pytcha()
    fPy6Service->setGeneralParams();   

    // fPy6Service->setCSAParams();
    // fPy6Service->setSLHAParams();
    // fPy6Service->setPYUPDAParams(false);
    
    //-- mstu(8) is set to NMXHEP in this dummy call (version >=6.404)
    call_pyhepc(1);

    //-- initialise random number generator: has been changed to be CMSSW compliant    
    //-- dcasrn overloaded: call to rluxgo not needed anymore
    //-- call_rluxgo(4,314159265,0,0);
    
    //-- initialise CASCADE parameters (default values)
    call_casini();

    //-- Read the parameters and pass them to the common blocks
    //-- call_steer();

    cout<<"retrieve cascade parameters from python configuration file"<<endl;

    //-- initialise CASCADE parameters (user values)

    //-- retrieve all the different sets 
    vector<string> AllSets = fParameters.getParameter<vector<string> >("parameterSets");

    //-- loop over the different sets
    for(unsigned i=0; i<AllSets.size();++i) {
    
      string Set = AllSets[i];
      vector<string> Para_Set = fParameters.getParameter<vector<string> >(Set);

      //-- loop over all the parameters and stop in case of mistake
      for(vector<string>::const_iterator itPara = Para_Set.begin(); itPara != Para_Set.end(); ++itPara) {
	
	if(!cascadeReadParameters(*itPara)) {
	  throw edm::Exception(edm::errors::Configuration,"CascadeError")
	    <<" Cascade did not accept the following parameter: \""<<*itPara<<"\""<<endl;
	}
      } //-- end loop over all the parameters
    } //-- end loop over the different sets

    cainpu.plepin = -fComEnergy/2; 
    cainpu.ppin = fComEnergy/2;

    cascadePrintParameters();
   
    //-- change standard parameters of CASCADE
    call_cascha();
    
    //-- change standard parameters of JETSET/PYTHIA
    //-- call_pytcha();

    //-- set up for running CASCADE (integration of the cross-section)
    call_cascade(); 

    //-- print cross-section result from integration
    call_caend(1);
 
    //fPy6Service->setPYUPDAParams(true);
    
    //fPy6Service->closeSLHA();
    
    return true;
  }

  bool Cascade2Hadronizer::declareStableParticles(vector<int> pdg){
   
    for(size_t i=0; i<pdg.size(); i++){
      int PyID = HepPID::translatePDTtoPythia(pdg[i]);
      int pyCode = pycomp_( PyID );

      if(pyCode > 0){
	ostringstream pyCard ;
	pyCard << "MDCY(" << pyCode << ",1)=0";
	//-- cout << "pdg= " << pdg[i] << " " << pyCard.str() << endl; 

	call_pygive(pyCard.str());
      }
    }
      
    return true;
  }

  void Cascade2Hadronizer::imposeProperTime(){
    
    // this is practically a copy/paste of the original code by J.Alcaraz, 
    // taken directly from PythiaSource
    
    int dumm=0;
    HepMC::GenEvent::vertex_const_iterator vbegin = event()->vertices_begin();
    HepMC::GenEvent::vertex_const_iterator vend = event()->vertices_end();
    HepMC::GenEvent::vertex_const_iterator vitr = vbegin;
    
    for(; vitr != vend; ++vitr){
      
      HepMC::GenVertex::particle_iterator pbegin = (*vitr)->particles_begin(HepMC::children);
      HepMC::GenVertex::particle_iterator pend = (*vitr)->particles_end(HepMC::children);
      HepMC::GenVertex::particle_iterator pitr = pbegin;
      
      for(; pitr != pend; ++pitr){
	
	if((*pitr)->end_vertex()) continue;
	if((*pitr)->status()!=1) continue;
        
	int pdgcode= abs((*pitr)->pdg_id());
	// Do nothing if the particle is not expected to decay
	if ( pydat3.mdcy[0][pycomp_(pdgcode)-1] !=1 ) continue;
      
	double ctau = pydat2.pmas[3][pycomp_(pdgcode)-1];
	HepMC::FourVector mom = (*pitr)->momentum();
	HepMC::FourVector vin = (*vitr)->position();
	double x = 0.;
	double y = 0.;
	double z = 0.;
	double t = 0.;
	bool decayInRange = false;
	while(!decayInRange){

	  double unif_rand = fPy6Service->call(pyr_, &dumm);
	  // Value of 0 is excluded, so following line is OK
	  double proper_length = - ctau * log(unif_rand);
	  double factor = proper_length/mom.m();
	  x = vin.x() + factor * mom.px();
	  y = vin.y() + factor * mom.py();
	  z = vin.z() + factor * mom.pz();
	  t = vin.t() + factor * mom.e();
	  
	  // Decay must be happen outside a cylindrical region
	  if(pydat1.mstj[21]==4){
	    if (sqrt(x*x+y*y)>pydat1.parj[72] || fabs(z)>pydat1.parj[73]) decayInRange = true;
	    // Decay must be happen outside a given sphere
	  } 
	  else if (pydat1.mstj[21]==3){
	    if (sqrt(x*x+y*y+z*z)>pydat1.parj[71]) decayInRange = true;
	  } 
	  // Decay is always OK otherwise
	  else {
	    decayInRange = true;
	  }
	}
                  
	HepMC::GenVertex* vdec = new HepMC::GenVertex(HepMC::FourVector(x,y,z,t));
	event()->add_vertex(vdec);
	vdec->add_particle_in((*pitr));
      }
    }   
    
    return;
  }

  void Cascade2Hadronizer::statistics(){

    if(!runInfo().internalXSec()){
  
      // set xsec if not already done (e.g. from LHE cross section collector)
      double cs = pypars.pari[0]; // cross section in mb
      cs *= 1.0e9; // translate to pb (CMS/Gen "convention" as of May 2009)
      runInfo().setInternalXSec( cs );
      // FIXME: can we get the xsec statistical error somewhere?
    }

    //--  print out generated event summary
    call_caend(2);

    //-- write out some information from Pythia to the screen
    call_pystat(1);
  
    return;
  }

  const char* Cascade2Hadronizer::classname() const {
    return "gen::Cascade2Hadronizer";
  }

  //-- Read the parameters and pass them to the common blocks
 
  bool Cascade2Hadronizer::cascadeReadParameters(const string& ParameterString) {

    bool accepted = 1;

    if(!strncmp(ParameterString.c_str(),"KE",2))
      caluco.ke = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"IRES(1)",7))
      capar6.ires[0] = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"KP",2))
      caluco.kp = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"IRES(2)",7))
      capar6.ires[1] = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"NFRAG",5))
      cainpu.nfrag = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"IPST",4))
      cashower.ipst = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"IPSIPOL",7))
      jpsi.ipsipol = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    //-- from version 2.2.03 on
    // else if(!strncmp(ParameterString.c_str(),"I23S",4))
    //  jpsi.i23s = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);
    
    else if(!strncmp(ParameterString.c_str(),"IFPS",4))
      cainpu.ifps = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"ITIMSHR",7))
      casshwr.itimshr = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"IRUNAEM",7))
      capar1.irunaem = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"IQ2",3))
      capar1.iq2 = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"IPRO",4))
      capar1.ipro = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"NFLAV",5))
      caluco.nflav = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"INTER",5))
      cainpu.inter = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"IHFLA",5))
      cahflav.ihfla = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"IRPA",4))
      cascol.irpa = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"IRPB",4))
      cascol.irpb = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"IRPC",4))
      cascol.irpc = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"ICCFM",5))
      casshwr.iccfm = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"IGLU",4))
      cagluon.iglu = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"IRspl",5))
      casprre.irspl = atoi(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"PT2CUT",6))
      captcut.pt2cut[capar1.ipro] = atof(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"ACC1",4))
      integr.acc1 = atof(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"ACC2",4))
      integr.acc2 = atof(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);      

    else if(!strncmp(ParameterString.c_str(),"SCALFAF",7))
      scalf.scalfaf = atof(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"SCALFA",6))
      scalf.scalfa = atof(&ParameterString[strcspn(ParameterString.c_str(),"=")+1]);

    else if(!strncmp(ParameterString.c_str(),"PDFPATH",7))
      strcpy(caspdf.pdfpath,&ParameterString[strcspn(ParameterString.c_str(),"=")+1]); 

    else accepted = 0;
    
    return accepted;
  }
  
  void Cascade2Hadronizer::cascadePrintParameters() {
   
    cout<<"flavour code of beam1: "<<caluco.ke<<endl; 
    cout<<"direct or resolved particle 1: "<<capar6.ires[0]<<endl; 
    cout<<"flavour code of beam2: "<<caluco.kp<<endl;    
    cout<<"direct or resolved particle 2: "<<capar6.ires[1]<<endl;  
    cout<<"fragmentation: "<<cainpu.nfrag<<endl;  
    cout<<"keep track of intermediate state in PS: "<<cashower.ipst<<endl;  
    cout<<"polarisation for J/psi: "<<jpsi.ipsipol<<endl;
    // cout<<"select state for vector meson: "<<jpsi.i23s<<endl;
    cout<<"parton shower: "<<cainpu.ifps<<endl;   
    cout<<"switch for time-like shower in initial state cascade: "<<casshwr.itimshr<<endl;
    cout<<"switch for running alphas: "<<capar1.irunaem<<endl;  
    cout<<"scale for alphas: "<<capar1.iq2<<endl;   
    cout<<"process number: "<<capar1.ipro<<endl;  
    cout<<"number of flavors in pdfs: "<<caluco.nflav<<endl;   
    cout<<"mode of interaction for ep: "<<cainpu.inter<<endl; 
    cout<<"flavor code for heavy flavor (IPRO = 11), for VM (IPRO = 2,3): "<<cahflav.ihfla<<endl; 
    cout<<"switch to select QCD process g* g* -> q qbar: "<<cascol.irpa<<endl;   
    cout<<"switch to select QCD process g* g -> g g: "<<cascol.irpb<<endl; 
    cout<<"switch to select QCD process g* q -> g q: "<<cascol.irpc<<endl;   
    cout<<"select CCFM or DGLAP mode: "<<casshwr.iccfm<<endl; 
    cout<<"select uPDF: "<<cagluon.iglu<<endl;  
    cout<<"switch for p-remnant treatment: "<<casprre.irspl<<endl;  
    cout<<"pz of incoming beam 1: "<<cainpu.plepin<<endl; 
    cout<<"pz of incoming beam 2: "<<cainpu.ppin<<endl;  
    cout<<"pt2 cut in ME for massless partons: "<<captcut.pt2cut[capar1.ipro]<<endl; 
    cout<<"accurary requested for grid optimisation step (BASES): "<<integr.acc1<<endl;  
    cout<<"accuracy requested for integration step (BASES): "<<integr.acc2<<endl; 
    cout<<"scale factor for scale in alphas: "<<scalf.scalfa<<endl; 
    cout<<"scale factor for final state parton shower scale: "<<scalf.scalfaf<<endl;
    cout<<"path where updf grid files are stored: "<<caspdf.pdfpath<<endl;
  }

} //-- namespace gen
