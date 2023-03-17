#ifndef EPOC_H
#define EPOC_H

#include <iostream>
#include <map>
#include <vector>
#include <set>
#include <string>
#include <algorithm>
#include "constant.h"
using namespace std;

class EpocManager{
    private:
    IntPtr epoc_size = 10;
    IntPtr curr = 0;
    public:
    EpocManager(IntPtr curr){
        epoc_size = epoc_size * EPOC_SCALE;
        this->curr = curr;
    }

    bool tick(IntPtr now){
        IntPtr total = now-curr+1;
        IntPtr rem = total % epoc_size;
        if(rem < total){
            curr = now;
            return true;
        }
        return false;
    }
};

#endif