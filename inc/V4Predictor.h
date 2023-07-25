#ifndef V4_PRED_H
#define V4_PRED_H

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

class V4Predictor: public IPredictor
{
    private:

    class DeadAndLive{
        public:
        DeadAndLive(){
            dead_wb=dead_fill=dead=alive=0;
        }
        IntPtr dead, alive, dead_wb, dead_fill;
        PACKET_LIFE pc_status(){return dead>alive?PACKET_LIFE::DEAD: PACKET_LIFE::ALIVE;}
    };

    // pc to dead block count
    map<IntPtr, DeadAndLive> gate_to_life;

    // dead count intensity of Page
    map<IntPtr, DeadAndLive> gate;

    // prediction for Page
    map<IntPtr, Status> judgement;

    bool prediction_warmup_finish;
    Coverage *coverage;
    
    public:
    V4Predictor(){
        prediction_warmup_finish=false;
        coverage = new Coverage();
        NAME="V4";
    }
    
    void insert(PACKET& pkt){
       
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

    /*
    packet, writeback
    */
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
            if(wrtype==WRITE_TYPE::FILL){
                gate_to_life[key].dead_fill++;
            }
            else{
                gate_to_life[key].dead_wb++;
            }
        }
    }

    /*
     1. using dead intensity to predict dead or alive
    */
    void epoc_end_judgement_day(IntPtr cycle){
        // get average dead count per page
        IntPtr sum = 0;
        for(auto entry: gate_to_life){
            sum += entry.second.dead;
        }
        double avg = (double)sum/(double)gate_to_life.size();

        // set them dead or alive if they are above avg number of deads
        for(auto entry: gate_to_life){
            if(judgement.find(entry.first)==judgement.end()){
                judgement[entry.first]=Status();
            }
            if(entry.second.dead > avg){
                judgement[entry.first].set_var_prediction(PREDICTION::DEAD);
            }else{
                judgement[entry.first].set_var_prediction(PREDICTION::ALIVE);
            }
        }

        // for(auto entry: gate_to_life){
        //     if(judgement.find(entry.first)==judgement.end()){
        //         printf("should not be the case!!!\n"); 
        //         // is it possible
        //         continue; 
        //     }
        //     PACKET_LIFE pkt_life = entry.second.pc_status();
        //     judgement[entry.first].set_var_dead_or_alive(pkt_life);
        //     PREDICTION prediction = static_cast<PREDICTION>(judgement[entry.first].get_prediction());
        //     add_prediction_health(prediction, pkt_life);

        // }

        if(!prediction_warmup_finish)
            prediction_warmup_finish=true;
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

    void print(IntPtr cycle = 0, string tag="end"){
        
        if(tag == "end"){
            string s = "pc_info_v4.log";
            fstream f = Log::get_file_stream(s);
            f<<"pc,alive,dead,dead-fill,dead-wb\n";
            // at the end of simulation
            for(auto entry: gate_to_life){
                string out = to_string(entry.first) +","+to_string(entry.second.alive)+","
                +to_string(entry.second.dead)+","+to_string(entry.second.dead_fill)+","+to_string(entry.second.dead_wb)
                +"\n";
                f << out;
            }
            f.close();
        }
        else{
           
        }
        {
            string s = "predictor_health_v4.log";
            fstream fph=Log::get_file_stream(s);
            fph << "fn,fp,tn,tp\n";
            fph << coverage->health() << '\n';
        }
    }


};

#endif