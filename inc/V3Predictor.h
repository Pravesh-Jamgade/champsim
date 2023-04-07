#ifndef V3_PRED_H
#define V3_PRED_H

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

typedef map<IntPtr, IntPtr> SecondLevel;
typedef map<IntPtr, Status> JudgeLevel;

class V3Predictor: public IPredictor{
    
    private:
    class DeadAndLive{
        public:
        DeadAndLive(){
            dead=alive=0;
        }
        IntPtr dead, alive;
        PACKET_LIFE pc_status(){return dead>alive?PACKET_LIFE::DEAD: PACKET_LIFE::ALIVE;}
    };

    typedef map<IntPtr, DeadAndLive> LifeLevel;

    map<IntPtr, SecondLevel> two_level_gate;
    map<IntPtr, LifeLevel> gate_to_life;
    map<IntPtr, JudgeLevel> judgement;
    Coverage *coverage;
    bool prediction_warmup_finish;
    public:
    V3Predictor(){
        coverage = new Coverage();
        prediction_warmup_finish=false;
        NAME="V3";
    }

    /*
    1.increasing write intensity counter
    */
    void insert(PACKET& pkt){
        IntPtr key1 = pkt.address >> LOG2_PAGE_SIZE;
        IntPtr key2 = pkt.pc;

        auto foundKey1 = two_level_gate.find(key1);
        // page not found
        if(foundKey1 == two_level_gate.end()){
            two_level_gate[key1][key2] = 1;
        }
        // page found
        else{
            auto foundKey2 = foundKey1->second.find(key2);
            // pc not found
            if(foundKey2 == foundKey1->second.end()){
                two_level_gate[key1][key2] = 1;
            }
            // pc found
            else{
                foundKey2->second++;
            }
        }
    }

    void print(IntPtr cycle = 0, string tag="end"){
        if(tag == "end"){
            string s = "pc_info_v3.log";
            fstream f = Log::get_file_stream(s);
            f << "page,pc,write,dead,alive\n";
            for(auto page: two_level_gate){
                auto foundPage = gate_to_life.find(page.first);
                for(auto pc: page.second){
                    int dead,alive;
                    dead=alive=-2;//page not found
                    if(foundPage!=gate_to_life.end()){
                        dead=alive=-1;//pc not found
                        auto findPC = gate_to_life[page.first].find(pc.first);
                        if(findPC!=gate_to_life[page.first].end()){
                            dead=gate_to_life[page.first][pc.first].dead;
                            alive=gate_to_life[page.first][pc.first].alive;
                        }    
                    }
                    f<<page.first<<","<<pc.first<<","<<pc.second<<","<<dead<<","<<alive<<'\n';
                }
            }
            f.close();
        }
        else{}
        {
            string s = "predictor_health_v3.log";
            fstream fph=Log::get_file_stream(s);
            fph << "fn,fp,tn,tp\n";
            fph << coverage->health() << '\n';
        }
    }

    void epoc_end_judgement_day(IntPtr cycle){
        IntPtr sum = 0;
        int count = 0;
        for(auto page: two_level_gate){
            for(auto pc: page.second){
                sum += pc.second;
            }
            count += page.second.size();
        }

        double avg = (double)sum/(double)count;

        for(auto page: two_level_gate){
            for(auto pc: page.second){
                judgement[page.first][pc.first] = Status();
            }
        }

        for(auto page: two_level_gate){
            for(auto pc: page.second){
                bool pred = pc.second > avg;//write intense
                if(pred)
                    judgement[page.first][pc.first].set_prediction(PREDICTION::ALIVE);
                else 
                    judgement[page.first][pc.first].set_prediction(PREDICTION::DEAD);
            }
        }

        for(auto page: gate_to_life){
            for(auto pc: page.second){
                PACKET_LIFE pkt_life = pc.second.pc_status();
                judgement[page.first][pc.first].set_dead_or_alive(pkt_life);
                PREDICTION prediction = judgement[page.first][pc.first].get_prediction();
                add_prediction_health(prediction, pkt_life);
            }
        }

        if(!prediction_warmup_finish)
            prediction_warmup_finish=true;
        
    }

    /*
    1.increase dead/alive counter
    */
    void insert_actual_life_status(PACKET& pkt, WRITE_TYPE wrtype=WRITE_TYPE::INVALID){
        IntPtr key1 = pkt.address >> LOG2_PAGE_SIZE;
        IntPtr key2 = pkt.pc;
        auto foundKey1 = gate_to_life.find(key1);
        PACKET_LIFE life_status = pkt.packet_life;

        // page not found
        if(foundKey1 == gate_to_life.end()){
            gate_to_life[key1][key2]=DeadAndLive();
        }
        // page found
        else{
            auto foundKey2 = foundKey1->second.find(key2);
            // pc not found
            if(foundKey2 == foundKey1->second.end()){
                gate_to_life[key1][key2]=DeadAndLive();
            }
            // pc found
            {

            }
        }
           
        if(life_status == PACKET_LIFE::ALIVE)
        {
            gate_to_life[key1][key2].alive++;
        }
        else if(life_status == PACKET_LIFE::DEAD){
            gate_to_life[key1][key2].dead++;
        }
    }

    /*
    1. checks if prediction table is warmup (1st epoc is done and predictions are ready to serve)
    2. checks if prediction for key's exists
    */
    PREDICTION get_judgement(PACKET& pkt){
        IntPtr key1 = pkt.address >> LOG2_PAGE_SIZE;
        IntPtr key2 = pkt.pc;
        PREDICTION pred = PREDICTION::NO_PREDICTION;
        if(!prediction_warmup_finish)
            return pred;
        
        auto findPage = judgement.find(key1);
        if(findPage!=judgement.end()){
           auto findPC = findPage->second.find(key2);
           if(findPC!=findPage->second.end()){
                pred = findPC->second.get_prediction();
           }
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
};

#endif