#ifndef UTILS_H
#define UTILS_H
#include <iostream>
// #include "cache.h"

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
    IS_Buffer,
    IS_DRAM,
    CACHE_ID_END
};

#endif