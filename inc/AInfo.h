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


/**
 * @brief Address access info
 * 
 */
class AAInfo{
    public:
    AAInfo(){}
    AAInfo(int pos){
        writes = 0;
        first_pos = last_pos = pos;
    }
    IntPtr writes;
    int first_pos, last_pos;

    vector<IntPtr> live_interval_len;
    vector<IntPtr> writes_seen;

    void set_len(){live_interval_len.push_back(last_pos-first_pos + 1);};
    void set_write(){writes_seen.push_back(writes);}
    void reset_pos(){
        last_pos = first_pos = 1e9;
    }
};

/**
 * @brief Address access table
 * 
 */


class AATable{

    public:
    /*address access map*/
    map<IntPtr, AAInfo> aamap;
    static int pos;

    // could be read and write for address (key)
    // hitmiss = 1 (hit); otherwise (miss)
    void insert(IntPtr key, bool hitmiss){
        pos++;

        // hit -> inc write set last pos; if it was evicted then set first pos as well
        // miss -> if it was earlier brought in cache set len, write; reset pos; otherwise its a miss and in MSHR/PQ and allocate eventually would be bought in.
        if(hitmiss){
            auto findA = aamap.find(key);
            if(findA!=aamap.end()){
                findA->second.writes++;
                if(findA->second.first_pos == findA->second.last_pos && findA->second.first_pos == 1e9){
                    findA->second.first_pos = pos;
                }
                findA->second.last_pos = pos;
            }
            else{
                aamap.insert({key, AAInfo(pos)});
            }
        }
        else{
            auto findA = aamap.find(key);
            if(findA!=aamap.end()){
                findA->second.set_len();
                findA->second.set_write();
                findA->second.reset_pos();
            }
            else{
                aamap.insert({key, AAInfo(pos)});
            }
        }
    }

    vector<string> process_final(vector<string>& all_addr){
        vector<string> all_str;

        for(auto data: aamap){
            IntPtr addr = data.first;
            double avg_len, avg_writes;
            avg_len=avg_writes=0;

            // address and len of interval before evicition
            for(auto entry: data.second.live_interval_len){
                avg_len += entry;
            }
            for(auto entry: data.second.writes_seen){
                avg_writes += entry;
            }

            string s = ",";

            vector<IntPtr> len_vec = data.second.live_interval_len;
            vector<IntPtr> write_vec = data.second.writes_seen;

            for(int i=0; i<  min(len_vec.size(), write_vec.size()); i++){
                string per_addr = to_string(data.first) +s+ to_string(len_vec[i]) +s+ to_string(write_vec[i]);
                all_addr.push_back(per_addr);
            }

            int len_size = (double)data.second.live_interval_len.size();
            int write_size = (double)data.second.writes_seen.size();
            string res = to_string(addr) + "," + to_string(avg_len) + "," + to_string(len_size) + "," + to_string(avg_writes) +","+ to_string(write_size);
            cout << res << '\n';
            all_str.push_back(res);
        }
        return all_str;
    }

};