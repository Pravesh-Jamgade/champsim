#ifndef PRED_HEL
#define PRED_HEL

#include <iostream>
#include <map>
#include <vector>
#include <set>
#include <string>
#include <algorithm>
#include "constant.h"
#include "log.h"

using namespace std;

class Coverage{
    private:
    IntPtr fn,fp,tn,tp;
    public:
    Coverage(){
        fn=fp=tn=tp=0;
    }

    void increase(STAT stat){
        switch(stat){
            case STAT::FP: fp++; break;
            case STAT::FN: fn++; break;
            case STAT::TP: tp++; break;
            case STAT::TN: tn++; break;
        }
    }
    IntPtr get(STAT stat){
        switch(stat){
            case STAT::FP: return fp; break;
            case STAT::FN: return fn; break;
            case STAT::TP: return tp; break;
            case STAT::TN: return tn; break;
        }
    }

    string health(){
        return to_string(fn)+","+to_string(fp)+","+to_string(tn)+","+to_string(tp);
    }
    
};

#endif 