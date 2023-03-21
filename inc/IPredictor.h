#ifndef IPRED_H
#define IPRED_H

#include <iostream>
#include <map>
#include <vector>
#include <set>
#include <string>
#include <algorithm>
#include "constant.h"
#include "log.h"
#include "memory.h"

using namespace std;

class Status{
    private:
    int prediction;
    int confidence;
    public:
    Status(){}
    Status(int pred){
        prediction = pred;
        confidence = 0;
    }
    int get_status(){return prediction;}
};

class IPredictor{
    public:
    virtual void insert(PACKET& pkt)=0;
    virtual void print(IntPtr cycle = 0, string tag="end")=0;
    virtual void epoc_end_judgement_day(IntPtr cycle)=0;
    virtual PREDICTION get_judgement(IntPtr key)=0;
    virtual void add_prediction_health(STAT stat)=0;

};
#endif