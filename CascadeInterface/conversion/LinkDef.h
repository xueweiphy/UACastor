#ifdef __CINT__

#pragma link C++ class HepMC::FourVector+;
#pragma link C++ class HepMC::ThreeVector+;
#pragma link C++ class HepMC::GenEvent+;
#pragma link C++ class HepMC::GenParticle+;
#pragma link C++ class HepMC::Flow+;
#pragma link C++ class HepMC::GenVertex+;
#pragma link C++ class HepMC::WeightContainer+;
#pragma link C++ class HepMC::HeavyIon+;
#pragma link C++ class HepMC::PdfInfo+; 
#pragma link C++ class HepMC::Polarization+;
#pragma link C++ class std::map<int,HepMC::GenVertex*,std::greater<int> >+;
#pragma link C++ class std::map<int,HepMC::GenParticle*>+;
#pragma link C++ class std::pair<int,HepMC::GenVertex*>+;
#pragma link C++ class std::pair<int,HepMC::GenParticle*>+;

#endif
