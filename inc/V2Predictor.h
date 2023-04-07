#ifndef V2_PRED_H
#define V2_PRED_H

#include <iostream>
#include <map>
#include <vector>
#include <set>
#include <string>
#include <algorithm>
#include "champsim_constants.h"
#include "constant.h"
#include "log.h"
#include "PredictorHealth.h"
#include "IPredictor.h"

using namespace std;

class V2Predictor: public IPredictor
{
    private:

    class DeadAndLive{
        public:
        DeadAndLive(){
            dead=alive=0;
        }
        IntPtr dead, alive;
        PACKET_LIFE pc_status(){return dead>alive?PACKET_LIFE::DEAD: PACKET_LIFE::ALIVE;}
    };

    // pc to dead block count
    map<IntPtr, DeadAndLive> gate_to_life;

    // write intensity of PC
    map<IntPtr, int> gate;
    // prediction for PC
    map<IntPtr, Status> judgement;
    set<IntPtr> seen_before;
    bool prediction_warmup_finish;
    Coverage *coverage;
    fstream epoc_data_fs;
    public:
    V2Predictor(){
        prediction_warmup_finish=false;
        coverage = new Coverage();
        NAME="V2";
    }
    /*
        Phase 1: 
        find out PC <--> Write pairs
    */
    void insert(PACKET& pkt){
        IntPtr key = pkt.address >> LOG2_PAGE_SIZE;
       auto foundKey = gate.find(key);
       if(foundKey!=gate.end()){
        gate[key]++;
       }
       else{
        gate.insert({key,1});
       }
    }

    /*
    
    */
    void print(IntPtr cycle = 0, string tag="end"){
        
        if(tag == "end"){
            string s = "pc_info_v2.log";
            fstream f = Log::get_file_stream(s);
            f<<"pc,write,dead,alive\n";
            // at the end of simulation
            for(auto entry: gate){
                string dead_alive = ",-1,-1";//page not found
                auto findPC = gate_to_life.find(entry.first);
                if(findPC!=gate_to_life.end()){
                    dead_alive = "," + to_string(findPC->second.dead) + "," + to_string(findPC->second.alive);
                }
                string out = to_string(entry.first) +","+to_string(entry.second)+ dead_alive +"\n";
                f << out;
            }
            f.close();
        }
        else{
           
        }
        {
            string s = "predictor_health_v2.log";
            fstream fph=Log::get_file_stream(s);
            fph << "fn,fp,tn,tp\n";
            fph << coverage->health() << '\n';
        }
        
    }

    /*
     1. using write intensity to predict dead or alive
    */
    void epoc_end_judgement_day(IntPtr cycle){
        // get average write per pc
        IntPtr sum = 0;
        for(auto entry: gate){
            sum += entry.second;
        }
        double avg = (double)sum/(double)gate.size();

        // set them dead or alive if they are above avg number of writes
        for(auto entry: gate){
            if(judgement.find(entry.first)==judgement.end()){
                judgement[entry.first]=Status();
            }
            if(entry.second > avg){
                judgement[entry.first].set_prediction(PREDICTION::ALIVE);// predicted to be write intensive
            }else{
                judgement[entry.first].set_prediction(PREDICTION::DEAD);
            }
        }

        for(auto entry: gate_to_life){
            if(judgement.find(entry.first)==judgement.end()){
                printf("should not be the case!!!\n"); 
                // is it possible
                continue; 
            }
            PACKET_LIFE pkt_life = entry.second.pc_status();
            judgement[entry.first].set_dead_or_alive(pkt_life);
            PREDICTION prediction = static_cast<PREDICTION>(judgement[entry.first].get_prediction());
            add_prediction_health(prediction, pkt_life);

        }

        if(!prediction_warmup_finish)
            prediction_warmup_finish=true;
        
        // in order to analyze epoc wise, we should reset existing gate map, as it will have write intensity of previous
        // epocs data
        // print(cycle, "epoc");
    }

    /*
    1. checks if prediction table is warmup (1st epoc is done and predictions are ready to serve)
    2. checks if prediction for key exists
    */
    PREDICTION get_judgement(PACKET& pkt){
        IntPtr key = pkt.address >> LOG2_PAGE_SIZE;
        PREDICTION pred = PREDICTION::NO_PREDICTION;
        if(!prediction_warmup_finish)
            return pred;
        
        if(judgement.find(key)!=judgement.end()){
            pred=static_cast<PREDICTION>(judgement[key].get_prediction());
        }
        return pred;
    }

    void add_prediction_health(PREDICTION prediction, PACKET_LIFE actual){
       // Dead : it is when i am going to bypass
       // if predicted dead (which we ar going to use to bypass) but actually alive is goig to harm our results. (bad case)
       // this is our false-positive opposite is false-negative (it will not harm but although its a miss opportunity)
        if(actual == PACKET_LIFE::ALIVE){//actual
          switch(prediction){//result
            case PREDICTION::ALIVE:
              coverage->increase(STAT::TP);
              break;
            case PREDICTION::DEAD:
              coverage->increase(STAT::FP);
              break;
          }
        }
        else if(actual == PACKET_LIFE::DEAD){
          switch(prediction){
            case PREDICTION::ALIVE:
              coverage->increase(STAT::FN);
              break;
            case PREDICTION::DEAD:
              coverage->increase(STAT::TN);
              break;
          }
        }
    }

    void insert_actual_life_status(PACKET& pkt, WRITE_TYPE wrtype=WRITE_TYPE::INVALID){
        IntPtr key = pkt.address >> LOG2_PAGE_SIZE;
        PACKET_LIFE life_status = pkt.packet_life;
        if(gate_to_life.find(key)==gate_to_life.end()){
            gate_to_life[key]=DeadAndLive();
        }
        if(life_status == PACKET_LIFE::ALIVE)
        {
            gate_to_life[key].alive++;
        }
        else if(life_status == PACKET_LIFE::DEAD){
            gate_to_life[key].dead++;
        }
    }

};

#endif