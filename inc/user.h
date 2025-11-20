#ifndef USER_H
#define USER_H
#include "operable.h"
#include <list>

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

// class PTEclass
// {
//     public:
//     uint64_t vaddr, paddr;
//     int thread_id;
    
//     PTEclass(){}
//     PTEclass(uint64_t vaddr, uint64_t paddr, int thread_id)
//     {
//         this->vaddr = vaddr;
//         this->paddr = paddr;
//         this->thread_id = thread_id;
//     }
// };

// // represent list of PTE 
// class PTEContainer
// {
//     public:
//     std::list<PTEclass> collection;
//     PTEContainer(){}
//     void push(PTEclass pte)
//     {
//         collection.push_back(pte);
//     }

//     int size(){return collection.size();}
//     bool full(){return collection.size() == 8;}
//     PTEclass front(){return collection.front();}
//     void pop_front(){collection.pop_front();}
// };

// // represent each thread, holding 8 possible offsets
// class ThreadBucket
// {
//     PTEContainer pte_container[8];
//     public:
//     ThreadBucket()
//     {
//         for(int i=0; i< 8; i++)
//             pte_container[i] = PTEContainer();
//     }

//     // if no space to insert the PTE then false
//     void insert(int off, PTEclass pte)
//     {
//         pte_container[off].push(pte);
//     }

//     bool isSpaceAvail(int off)
//     {
//         return pte_container[off].full();
//     }

//     // test if cluster of size = 8 is avail
//     pair<bool, PTEContainer> test()
//     {
//         PTEContainer buffer;
//         // 8 offsets, hence 8 containers
//         uint8_t avail = 0;
//         for(int i=0; i< 8; i++)
//         {   
//             if(pte_container[i].size()>0)
//             {
//                 // set bit
//                 avail |= 1 << i;
//             }
//         }

//         int count = __builtin_popcount(avail);

//         // available
//         if(count == 8)
//         {
//             // now collect them in buffer
//             for(int i=0; i< 8; i++)
//             {
//                 buffer.push(pte_container[i].front());
//                 pte_container[i].pop_front();
//             }
//         }
//         return make_pair(count == 8, buffer);
//     }
// };
enum DataType
{
    DATA=0,
    PTE,
    PMD,
    PUD,
    PGD,
    PRE,
    INVALID,
    DataType_end
};

#endif