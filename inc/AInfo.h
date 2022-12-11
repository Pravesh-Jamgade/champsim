/**
 * @file AInfo.h (access info at access) 
 * @author Pravesh Jamgade
 * @brief 
 * @version 0.1
 * @date 2022-11-02
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include <iostream>
#include <map>
#include <vector>
#include <set>
#include "constant.h"

#define SIZE 1000000

using namespace std;

// // CACHE ACCESS TYPE
// #define LOAD 0
// #define RFO 1
// #define PREFETCH 2
// #define WRITEBACK 3
// #define TRANSLATION 4
// #define NUM_TYPES 5
enum WriteType{
    A=0,
    B,
    C,
    D,
    E,
    F,
};

class WType
{
    public:
    //  all of these writes will be added in WQ
    //  0->core write, demand-miss write, prefetch-write
    IntPtr wr_type[6];
    WType(){
        wr_type[6] = {0};
    }

    string get_type(int i){
        switch(i){
            case 0: return "LOAD";
            case 1: return "RFO";
            case 2: return "PREFETCH";
            case 3: return "WRITEBACK";
            case 4: return "TRANSLATION";
            case 5: return "NUM_TYPES";
        }
    }

    void inc(WriteType wt){
        wr_type[wt]++;
    }

    vector<string> print(){
        vector<string> vec;
        // for(int i=0 ;i< 6; i++){
        //     string st = "";
        //     st = get_type(i) + "," + to_string(wr_type[i]);
        //     vec.push_back(st);
        // }
        return vec;
    }
};


class Count{
    public:
    int fills;
    int writebacks;
    int score;
    Count(){
        score=writebacks=0;
        fills=1;
    }
    int get_score(){return score;}
};


/**
 * @brief Address access table
 * 
*/
class AATable{

    public:

    WType type_of_writes;
    static int pos;
    map<IntPtr, Count> prediciton;//1->write 0->dead
    int thresh = 5;
    IntPtr prev_diff = 0;

    string fileName="epoc.log";
    FILE* epoc_fs = fopen(fileName.c_str(), "w");

    AATable(){}

    int insert(IntPtr pc, int req_type){
        auto findPC = prediciton.find(pc);
        if(findPC==prediciton.end()){
            prediciton.insert({pc, Count()});
            return -1;
        }

        // evict
        if(req_type==0){
            findPC->second.score -= 1;
            findPC->second.writebacks++;
        }
        // insert
        else{
            findPC->second.score += 2;
            findPC->second.fills++;
        }
        return findPC->second.get_score() > thresh ? 1:0;
    }

    void decrease_score(IntPtr cycle){
        IntPtr diff = cycle % SIZE;
        if(diff < prev_diff){
            cycle = cycle;
            prev_diff = 0;
            
            fprintf(epoc_fs, "%ld,%d\n", cycle, prediciton.size());
            for(auto e: prediciton){
                fprintf(epoc_fs, "%ld,%ld,%ld\n", e.first, e.second.score, cycle);
            }
            for(auto e: prediciton){
                if(e.second.score < 0)
                    continue;
                e.second.score--;
            }

            return;
        }
        prev_diff = diff;
    }

    // req_type: 0->evicted 1->inserted
    //return notfound-1,dead0,intense1
    int update_lx(IntPtr addr, int req_type){
        return insert(addr, req_type);
    }

    void increase_write_count(int t){
        type_of_writes.inc(static_cast<WriteType>(t));
    }

    vector<string> get_addr_loop(){
        vector<string> all_res;
        for(auto m: prediciton){
            string res = ""+to_string(m.first)+","+to_string(m.second.fills)+","+to_string(m.second.writebacks);
            all_res.push_back(res);
        }
        return all_res;
    }
 
};

