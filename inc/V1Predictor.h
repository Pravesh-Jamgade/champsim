#ifndef V1_PRED_H
#define V1_PRED_H

#include <iostream>
#include <map>
#include <vector>
#include <set>
#include <string>
#include <algorithm>
#include "constant.h"
#include "log.h"
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

class V1Predictor
{
    private:

    map<IntPtr, int> gate;
    map<IntPtr, Status> judgement;

    public:
    /*
        Phase 1: 
        find out PC <--> Write pairs
    */
    void insert(IntPtr key){
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
        string s = "epoc_"+tag+".log";
        fstream f = Log::get_file_stream(s);

        if(tag == "end"){
            // at the end of simulation
            for(auto entry: gate){
                string out = to_string(entry.first) +","+to_string(entry.second)+"\n";
                f << out;
            }
        }
        else{
            // after each epoc end
            int active, dead;
            active=dead=0;
            for(auto entry: judgement){
                if(entry.second.get_status() == PREDICTION::ALIVE) active++;
                else dead++; 
            }
            string out = to_string(cycle) + "," +to_string(active) + "," +to_string(dead) +"\n";
            f << out;
        }

        f.close();
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
        
        // in order to analyze epoc wise, we should reset existing gate map, as it will have write intensity of previous
        // epocs data
        // print(cycle, "epoc");

    }

    PREDICTION get_judgement(IntPtr key){
        PREDICTION pred = PREDICTION::DEAD;
        if(judgement.find(key)!=judgement.end()){
            pred=static_cast<PREDICTION>(judgement[key].get_status());
        }
        return pred;
    }

};

#endif