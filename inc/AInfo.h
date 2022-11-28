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
        for(int i=0 ;i< 6; i++){
            string st = "";
            st = get_type(i) + "," + to_string(wr_type[i]);
            vec.push_back(st);
        }
        return vec;
    }
};


class AAinfo{
    public:
    int writes;

};


/**
 * @brief Loop Addresses 
 * 
 */
class LoopAddr
{
    public:
    LoopAddr(){}

    map<IntPtr, IntPtr> score;
    void insert(IntPtr pc, bool req_type){
        auto findPC = score.find(pc);
        // eviction
        if(req_type == 0){
            
            if(findPC != score.end()){
                findPC->second--;
            }
            else{
                // during evixtion should not have happened;
            }
        }
        // first time insert
        else{
            
            if(findPC != score.end()){
                findPC->second+=2;
            }
            else{
                score.insert({pc, 2});
            }
        }
    }
};

/**
 * @brief Address access table
 * 
*/
class AATable{

    public:

    LoopAddr *ll;

    WType type_of_writes;
    static int pos;

    AATable(){
        ll = new LoopAddr();
    }

    void increase_write_count(int t){
        type_of_writes.inc(static_cast<WriteType>(t));
    }

    // req_type: 0->evicted 1->inserted
    void update_lx(IntPtr addr, bool req_type){
        ll->insert(addr, req_type);
    }

    vector<string> get_addr_loop(){
        vector<string> all_res;
        for(auto m: ll->score){
            string res = ""+to_string(m.first)+","+to_string(m.second);
            all_res.push_back(res);
        }
        return all_res;
    }
 
};

