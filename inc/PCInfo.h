#include <iostream>
#include <map>
#include <fstream>

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
        epocLength = SIZE;
        epoc_type = false; // learning
        this->cycle = cycle;
        this->prev_diff = 0;
    }

    bool tick(IntPtr curr_cycle){
        IntPtr diff = curr_cycle % SIZE;
        if(diff < prev_diff){
            cycle = curr_cycle;
            prev_diff = 0;
            return true;
        }
        prev_diff = diff;
        return false;
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
    map<IntPtr, EpocData> bypass;
    map<IntPtr, bool> bypass_pred;

    bool use_pred = false;
    EpocManager epoc_mgr;

    FILE* out_fs;

    PCinfo(){}
    PCinfo(IntPtr cycle){
        epoc_mgr = EpocManager(cycle);
        out_fs = fopen("epoc.log", "w");
    }

    // result hit->1, miss->0
    bool feed(IntPtr pc, bool result, bool& should_use_pred, IntPtr cc){

        auto findpc = bypass.find(pc);
        if(findpc==bypass.end()){
            bypass[pc]=EpocData();
        }

        // learn for first 100 access(read/write) of application start then do both learn and apply
        bypass[pc].inc_access();
        if(!result){
            bypass[pc].inc_miss();
        }
        
       
        // if it is end of epoc then calculate predicition 
        if(epoc_mgr.tick(cc)){
            fprintf(out_fs, "%ld\n", epoc_mgr.cycle);
            // calculate avg miss ratio per pc
            double avg_miss = 0;
            for(auto pc: bypass){
                double mr = pc.second.cal_miss_ratio();
                avg_miss += mr;
            }
            avg_miss /= bypass.size();

            // update predition; miss ratio above avg is bypass otherwise stay
            for(auto pc: bypass){
                bypass_pred[pc.first] = pc.second.cal_miss_ratio() > avg_miss ? true: false;
            }
            bypass.clear(); // clear to accumulate data for next epoc
            use_pred = true; // start using prediciton right after end of first epoc
        }

        should_use_pred = use_pred;

        if(use_pred){
            auto find_pred = bypass_pred.find(pc);
            if(find_pred!=bypass_pred.end())
                return bypass_pred[pc];
        }

        return false;
    }
};
