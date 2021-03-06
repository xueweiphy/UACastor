#ifndef MyJetPtSorter_h
#define MyJetPtSorter_h

#include "./MyJet.h"

class MyJetPtSorter {

 public:

  bool operator() ( const MyJet& a, const MyJet& b ) {
    return ( a.pt_cal > b.pt_cal );
  }
  
};

#endif
