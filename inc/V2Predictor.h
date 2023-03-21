#ifndef V2PRED_H
#define V2PRED_H

#include <iostream>
#include <map>
#include <vector>
#include <set>
#include <string>
#include <algorithm>
#include "constant.h"
#include "log.h"
#include "PredictorHealth.h"
#include "IPredictor.h"
#include "block.h"

using namespace std;

class V2Predictor: public IPredictor
{
    private:
    map<IntPtr, int> gate;
    map<IntPtr, Status> judgement;
    bool prediction_warmup_finish;
    Coverage *coverage;
    fstream epoc_data_fs; 

    public:
    V2Predictor(){
        prediction_warmup_finish=false;
        coverage = new Coverage();
        string s = "epoc_write_instense_number_of_sets.log";
        epoc_data_fs = Log::get_file_stream(s);
    }

    /*
        Phase 1: 
        find out page <--> Write pairs
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
            string s = "epoc_"+tag+".log";
            fstream f = Log::get_file_stream(s);
            f << "page,count\n";
            // at the end of simulation
            for(auto entry: gate){
                string out = to_string(entry.first) +","+to_string(entry.second)+"\n";
                f << out;
            }
            f.close();
        }
        else{
            // after each epoc end
            int active, dead;
            active=dead=0;
            for(auto entry: judgement){
                if(entry.second.get_status() == PREDICTION::ALIVE) active++;
                else dead++; 
            }
        }
        {
            string s = "predictor_health.log";
            fstream fph=Log::get_file_stream(s);
            fph << "fn,fp,tn,tp\n";
            fph << coverage->health() << '\n';
        }
        
    }

    /*
     
    */
    void epoc_end_judgement_day(IntPtr cycle){
        // get average write per pc
        IntPtr sum = 0;
        for(auto entry: gate){
            sum += entry.second;
        }
        double avg = (double)sum/(double)gate.size();

        // sort decreasing by count
        vector<pair<IntPtr, IntPtr>> count_to_pc;
        for(auto entry: gate){
            count_to_pc.push_back(make_pair(entry.second, entry.first));
        }
        sort(count_to_pc.begin(), count_to_pc.end());
        reverse(count_to_pc.begin(), count_to_pc.end());

        // pick top 10 largest write intensive PC
        int top_pick = 10;
        for(auto entry: count_to_pc){
            if(entry.first > avg && top_pick){
                judgement[entry.second]=Status(PREDICTION::ALIVE);// predicted to be write intensive
                top_pick--;
            }else{
                judgement[entry.second]=Status(PREDICTION::DEAD);
            }
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
    PREDICTION get_judgement(IntPtr key){
        PREDICTION pred = PREDICTION::NO_PREDICTION;
        if(!prediction_warmup_finish)
            return pred;
        
        if(judgement.find(key)!=judgement.end()){
            pred=static_cast<PREDICTION>(judgement[key].get_status());
        }
        return pred;
    }

    void add_prediction_health(STAT stat){
        coverage->increase(stat);
    }

};

#endif