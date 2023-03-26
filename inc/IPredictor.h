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
#include "block.h"
#include "champsim_constants.h"

using namespace std;

class Status{
    private:
    int prediction;
    int dead_or_alive;
    int confidence;
    public:
    Status(){
        prediction = PREDICTION::NO_PREDICTION;
        confidence = 0;
        dead_or_alive = static_cast<int>(PACKET_LIFE::INVALID);// these are set by actual observation when block is evicted
    }
    int get_status(){return prediction;}
    void set_prediction(int prediction){this->prediction=prediction;}
    void set_dead_or_alive(int actual){dead_or_alive=actual;}
    int get_dead_or_alive(){return dead_or_alive;}
};

class IPredictor{
    public:
    virtual void insert(PACKET& pkt)=0;
    virtual void print(IntPtr cycle = 0, string tag="end")=0;
    virtual void epoc_end_judgement_day(IntPtr cycle)=0;
    virtual PREDICTION get_judgement(PACKET& pkt)=0;
    virtual void add_prediction_health(PREDICTION prediction, PACKET_LIFE actual)=0;
    virtual void insert_actual_life_status(PACKET& pkt)=0;
};
#endif