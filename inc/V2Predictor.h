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
#define pii pair<IntPtr, int>

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

    //per page tracking all PC
    map<IntPtr, set<pii>> track_per_page_pc;
    map<IntPtr, IntPtr> pc_to_write;

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

       per_page_pc_tracking(key, pkt.pc);
    }

    void per_page_pc_tracking(IntPtr page, IntPtr pc){
        auto findPage = track_per_page_pc.find(page);
        if(findPage!=track_per_page_pc.end()){
            bool foundPC = false;
            for(auto e: findPage->second){
                if(e.first == pc){
                    foundPC = true;
                    e.second++; break;
                }
            }
            if(!foundPC){
                track_per_page_pc[page].insert({pc,1});
            }
        }else{
            track_per_page_pc[page].insert({pc,1});
        }

        if(pc_to_write.find(pc)!=pc_to_write.end()){
            pc_to_write[pc]++;
        }
        else{
            pc_to_write.insert({pc, 1});
        }
    }

    /*
    
    */
    void print(IntPtr cycle = 0, string tag="end"){
        string pc_count_file="pc_count.log";
        fstream pc_count_fs = Log::get_file_stream(pc_count_file);

        vector<int> top10;
        int ten=10;
        double avg_wr_per_pc=0;
        for(auto e: pc_to_write){
            avg_wr_per_pc += e.second;
            pc_count_fs << e.first << "," << e.second << '\n';
            if(ten--){
                top10.push_back(e.first);
            }
        }
        avg_wr_per_pc /= (double)pc_to_write.size();

        double total_writes = 0;
        double avg_writes = 0;
        
        // if gate_to_life entry not found
        // ***this tells block was inserted but never evicted***
        int count_no_entry_found = 0;
        if(tag == "end"){
            string s = "pc_info_v2.log";
            fstream f = Log::get_file_stream(s);
            f<<"pc,write,dead,alive,total_pc,top10,top_pc\n";
            // at the end of simulation
            for(auto entry: gate){
                string dead_alive = ",-1,-1";//page not found
                auto findPC = gate_to_life.find(entry.first);
                if(findPC!=gate_to_life.end()){
                    dead_alive = "," + to_string(findPC->second.dead) + "," + to_string(findPC->second.alive);
                }
                else count_no_entry_found++;
                string out = to_string(entry.first) +","+to_string(entry.second)+ dead_alive +",";
                
                // page to all pc's seen
                auto findPCPerPage = track_per_page_pc.find(entry.first);
                if(findPCPerPage!=track_per_page_pc.end()){
                    // find how many of tracked pc on each page are intense pc
                    int count_top_pc_per_page = 0;
                    int count_top10 = 0;
                    for(auto trackedPC: findPCPerPage->second){
                        // pc tracked on these page is write intensive if writes by it are above avg writes of pc 
                        if(pc_to_write[trackedPC.first] > avg_wr_per_pc){
                            count_top_pc_per_page++;
                        }
                        if(find(top10.begin(), top10.end(), trackedPC.first)!=top10.end()){
                            count_top10++;
                        }
                    }
                    out = out + to_string(findPCPerPage->second.size())+ ","+ to_string(count_top10) + "," + to_string(count_top_pc_per_page) + "\n";
                }
                                
                f << out;
                total_writes+=entry.second;
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

            avg_writes = total_writes/(double)gate.size();
            fph << "**************************************\n";
            fph << "\nTotal Writes of all Pages:" << total_writes;
            fph << "\nTotal pages:" << gate.size();
            fph << "\nAvg Writes Per Page:" << avg_writes;
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
            // compute prediction based on if alive_count > dead_count
            PACKET_LIFE pkt_life = entry.second.pc_status();
            // write intensity based prediction
            PREDICTION prediction = static_cast<PREDICTION>(judgement[entry.first].get_prediction());
            add_prediction_health(prediction, pkt_life);
            // update prediction previously calculated from write intensity by actual observance values of dead and alive count
            judgement[entry.first].set_dead_or_alive(pkt_life);
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