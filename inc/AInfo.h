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
    IntPtr wr_type[3];
    WType(){
        wr_type[3] = {0};
    }

    void inc(WriteType wt){
        wr_type[wt]++;
    }
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