{
  gROOT->ProcessLine(".L ../src/MyEvtId.cc+");
  gROOT->ProcessLine(".L ../src/MyL1Trig.cc+");
  gROOT->ProcessLine(".L ../src/MyGenKin.cc+");
  gROOT->ProcessLine(".L ../src/MyPart.cc+");
  gROOT->ProcessLine(".L ../src/MyGenPart.cc+");
  gROOT->ProcessLine(".L ../src/MyVertex.cc+");
  gROOT->ProcessLine(".L ../src/MyBeamSpot.cc+");
  gROOT->ProcessLine(".L ../src/MyCastorDigi.cc+");
  gROOT->ProcessLine(".L ../src/MyCastorJet.cc+");
  gROOT->ProcessLine(".L ../src/MyCastorRecHit.cc+");
  gROOT->ProcessLine(".L ../src/MyCastorTower.cc+");
  gROOT->ProcessLine(".L ../src/MyDiJet.cc+");
  gROOT->ProcessLine(".L ../src/MyJet.cc+");
  //gROOT->ProcessLine(".L ../src/MyJetPtSorter.cc+");
  gROOT->ProcessLine(".L ../src/MySimVertex.cc+");
  gROOT->ProcessLine(".L ../src/MyHLTrig.cc+"); 
  gROOT->ProcessLine(".L ../src/LinkDef.C+");
  gROOT->ProcessLine(".L ./FileReader.cc+");
  gROOT->ProcessLine(".L ./HistoRetriever.cc+");
  gROOT->ProcessLine(".L ./TreeAnalyzer.cc+");
  gROOT->ProcessLine(".L ./MCDataComparer.cc+");
}