#ifndef USER_H
#define USER_H
#include "operable.h"

using namespace std;
enum CACHE_ID{
    IS_LLC=0, 
    IS_L2, 
    IS_L1D, 
    IS_L1I, 
    IS_STLB, 
    IS_DTLB, 
    IS_ITLB,
    IS_DRAM,
    WQ,
    CACHE_ID_END// hit here means: page-fault only. Can we assume that ?
};

enum DataType
{
    DATA=0,
    PTE,
    PMD,
    PUD,
    PGD,
    VIC,
    PRE,
    INVALID,
    DataType_end
};

#endif