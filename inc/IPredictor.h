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

/* prediction, packet_life, confidence for PC*/
class Status{
    private:
    PREDICTION prediction;
    PACKET_LIFE dead_or_alive;
    int confidence;

    public:
    Status(){
        prediction = PREDICTION::NO_PREDICTION;
        //:TODO
        confidence = 0; // epocs based if dead=0 alive=7 (3-bit history counter), value resonate between 0-7
        dead_or_alive = PACKET_LIFE::INVALID;// these are set by actual observation when block is evicted
    }
    PREDICTION get_prediction(){
        switch(SUPER_USER_CONFIG_PREDICTION)
        {
            case (int)CONFIG_PREDICTION::WRITE_INTENSITY:
                return prediction;
                break;
            
            //:TODO
            case (int)CONFIG_PREDICTION::WRITE_WITH_DEADALIVE_HEURISTIC:
                exit(-1);
                break;
            
            case (int)CONFIG_PREDICTION::DEADALIVE_INTENSITY:
                return static_cast<PREDICTION>(dead_or_alive);
                break;
            
            default:
                printf("[ERROR] unknown\n");
                exit(-1);
                break;
        }
        return prediction;
    }
    void set_var_prediction(PREDICTION prediction){this->prediction=prediction;}
    void set_var_dead_or_alive(PACKET_LIFE actual){dead_or_alive=actual;}

    int get_var_prediction(){return (int)prediction;}
    int get_var_deadoralive(){return (int)dead_or_alive;}
};

class IPredictor{
    public:
    string NAME;
    virtual void insert(PACKET& pkt)=0;
    virtual void print(IntPtr cycle = 0, string tag="end")=0;
    virtual void epoc_end_judgement_day(IntPtr cycle)=0;
    virtual PREDICTION get_judgement(PACKET& pkt)=0;
    // virtual void add_prediction_health(PREDICTION prediction, PACKET_LIFE actual)=0;
    virtual void insert_actual_life_status(PACKET& pkt, WRITE_TYPE wrtype=WRITE_TYPE::INVALID)=0;
};
#endif