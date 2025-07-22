#ifndef USER_H
#define USER_H
enum CACHE_ID{
    IS_LLC=0, 
    IS_L2, 
    IS_L1D, 
    IS_L1I, 
    IS_STLB, 
    IS_DTLB, 
    IS_ITLB,
    WQ,
    CACHE_ID_END
};


#endif