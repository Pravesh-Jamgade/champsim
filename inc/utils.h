#ifndef UTILS_H
#define UTILS_H
#include <iostream>

using namespace std;
/*same as PACKET::HitWhere*/
enum CACHE_ID{
    IS_ITLB=0,
    IS_DTLB,
    IS_STLB,
    IS_L1I,
    IS_L1D,
    IS_L2,
    IS_LLC, 
    IS_DRAM,
    CACHE_ID_END
};

typedef long long ll;

#endif