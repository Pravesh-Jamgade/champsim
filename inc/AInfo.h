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

    void print(){
        cout << "Writes Type Count:\n";
        for(int i=0 ;i< 6; i++){
            cout << get_type(i) <<":"<< wr_type[i] << '\n';
        }
    }
};


class AAinfo{
    public:
    int writes;
        
};

/**
 * @brief Address access table
 * 
*/
class AATable{

    public:

    WType type_of_writes;
    static int pos;

    void increase_write_count(int t){
        type_of_writes.inc(static_cast<WriteType>(t));
    }

};