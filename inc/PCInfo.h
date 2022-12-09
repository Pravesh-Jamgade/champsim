#include <iostream>
#include <map>
#include "constant.h"

#define SIZE 1000000

using namespace std;

class EpocManager{
    public:
    bool epoc_type;
    IntPtr epocLength;// # memory references
    IntPtr cycle, prev_diff;
    EpocManager(){}
    EpocManager(IntPtr cycle){
        epocLength = 100;
        epoc_type = false; // learning
        this->cycle = cycle;
        this->prev_diff = 0;
    }

    void tick(IntPtr curr_cycle){
        IntPtr diff = (curr_cycle - cycle + 1) % SIZE;
        if(prev_diff > diff){
            epoc_type = true;
            cycle = curr_cycle;
        }
        prev_diff = diff;
            
        // if(epocLength>0) epocLength--;
        // else epocLength++;
        // if(epocLength == 0){
        //     epoc_type = true;
        // }
    }

    // false->learn; true->apply
    bool cal_predi(){
        return epoc_type;
    }
};

class EpocData{
    public:
    EpocData(){
        accesses=misses=0;
    }
    IntPtr accesses, misses;
    double miss_ratio;
    void inc_access(){accesses++;}
    void inc_miss(){misses++;}
    double cal_miss_ratio(){
        miss_ratio = (double)misses/(double)accesses;
        return miss_ratio;
    }
};

class PCinfo{
    public:

    bool use_pred = false;
    EpocManager epoc_mgr;
    int epoc;
    FILE *out_fs;

    PCinfo(){}
    PCinfo(IntPtr cycle){
        epoc_mgr = EpocManager(cycle);
        out_fs = fopen("epoc.log", "w");
        epoc = 0;
    }

    // not found->-1, dead->1, intense->2
    int feed(AATable aatable, IntPtr pc, IntPtr cc){

        // if it is end of epoc then calculate predicition 
        // if(epoc_mgr.tick(cc)){
        //     fprintf(out_fs, "%d, %ld\n", epoc++, epoc_mgr.cycle);
            // aatable.calculate(epoc); // calculate prediction for next epoc
            use_pred = true; // start using prediciton right after end of first epoc
        // }

        // if(use_pred){
        //     int pred = aatable.find_pred(pc);
        //     return pred;
        // }

        return -1;
    }
};
