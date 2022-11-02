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

#define IntPtr uint32_t
using namespace std;

class  AAFinal{

    public:
    vector<IntPtr> live_interval_len;
    vector<IntPtr> writes_seen;
};

/**
 * @brief Address access info
 * 
 */
class AAInfo{
    public:
    AAInfo(){}
    AAInfo(int pos){
        writes = last_pos = 0;
        first_pos = pos;
    }
    IntPtr writes;
    int first_pos, last_pos;
    int get_len(){return (last_pos-first_pos);};
};

/**
 * @brief Address access table
 * 
 */


class AATable{

    public:
    /*address access map*/
    map<IntPtr, AAFinal> aafinal;

    map<IntPtr, AAInfo> aamap;
    set<IntPtr> evicted_list;
    static int pos;

    // could be read and write for address (key)
    // op = 1 (write); otherwise (read)
    void insert(IntPtr key, bool op){
        pos++;

        if(op)
            return;
        
        if(evicted_list.size()!=0){
            auto findE = evicted_list.find(key);
            // address was evicted recently
            if(findE!=evicted_list.end()){
                evicted_list.erase(findE);
            }
        }

        auto findK = aamap.find(key);
        if(findK!=aamap.end()){
            findK->second.writes++;
        }
        else{
            aamap.insert({key,AAInfo(pos)});//with first store access postion
        }

        aamap[key].last_pos = pos; //with last store access position
    }

    // evict address from cache
    void evict(IntPtr key){

        // check if it was a live 
        auto findK = aamap.find(key);
        if(findK!=aamap.end()){

            int length = findK->second.get_len();
            IntPtr writes = findK->second.writes;

            // keep info about writes and len seen by address
            // it is out final results to analyze
            auto findf = aafinal.find(key);
            if(findf!=aafinal.end()){
                findf->second.live_interval_len.push_back(length);
                findf->second.writes_seen.push_back(writes);
            }
            else{
                auto temp = AAFinal();
                temp.live_interval_len.push_back(length);
                temp.writes_seen.push_back(writes);
                aafinal.insert({key, temp});
            }

            aamap.erase(findK);                 // removed from live list
            evicted_list.insert(findK->first);  // added to dead list
        }
        else{
            cout << "[err] evict address must have entry in address access map\n";
        }
    }

    vector<string> process_final(){
        vector<string> all_str;
        for(auto data: aafinal){
            IntPtr addr = data.first;
            IntPtr avg_len, avg_writes;
            for(auto entry: data.second.live_interval_len){
                avg_len += entry;
            }
            for(auto entry: data.second.writes_seen){
                avg_writes += entry;
            }
            avg_len /= data.second.live_interval_len.size();
            avg_writes /= data.second.writes_seen.size();
            string res = to_string(addr) + "," + to_string(avg_len) + "," + to_string(avg_writes);
            all_str.push_back(res);
        }
        return all_str;
    }

};