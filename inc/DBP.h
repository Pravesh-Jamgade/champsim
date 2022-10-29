// dead block prediction module

#include <iostream>
#include <map>
#include <fstream>
#include <vector>


#define IntPtr uint64_t
#define MAX 3
#define HMAX 1

#define UT 400
#define MT 200

using namespace std;

class Ptable{
    public:
    IntPtr writes;
    int sat;
    bool bypass;
    Ptable(){sat=bypass=0; writes=1;}

};

class DBPModule
{
    private:

    static int epoc_dbp;
    IntPtr wr_counter = 0;
    public:
    
    IntPtr coverage = 0; // how many predicted for bypass
                         // bypass count of both accurate, including those were in LLC and needed to invalidate them in LLC
    IntPtr accuracy = 0; // how many bypassed and were not in LLC and hence do not require invalidation
    
    map<IntPtr, Ptable> dbp_table;

    fstream dbp_stream;

    DBPModule(){
        dbp_stream.open("dbp.log", fstream::in  | fstream::app);
        coverage = accuracy = 0;
    }

    bool learning_period(){
        if(wr_counter < MT)
            return true;
        return false;
    }
    
    bool lookup(IntPtr pc, string cache_name){
        // if it is other cache, then "true" means data is here do as usual operation
        // and it will be same for LLC as well;
        // but if is "false" then prediction is only on LLC and data is now there and do "writeback"
        // use prediction only when learning period is over
        if(cache_name != "LLC" || learning_period())
            return true;
        auto find_pc = dbp_table.find(pc);
        if(find_pc!=dbp_table.end()){
            coverage++;
            bool no_bypass = find_pc->second.bypass;// pc based prediciton
            if(!no_bypass)
                accuracy++;
            return no_bypass;
        }

        // see if we need to reset (if usage thresold is reached)
        reset();

        return true;// do usual
    }

    void reset(){
        if(wr_counter < UT)
            return;
    
        vector<IntPtr> clean_list;
        IntPtr avg = wr_counter / dbp_table.size();
        for(auto temp: dbp_table){
            bool clean = temp.second.writes <= avg;
            if(clean){
                clean_list.push_back(temp.first);
            }
        }

        wr_counter = 0;
        coverage = 0;
        accuracy = 0;

        
        dbp_stream << epoc_dbp << "," << coverage << "," << accuracy << '\n';
        cout <<"[LOG]"<< epoc_dbp << "," << coverage << "," << accuracy << '\n';
        epoc_dbp++ ;

        for(auto pc: clean_list){
            dbp_table.erase(pc);
        }
    }

    // upon access miss
    void upon_access_miss(IntPtr pc){
        auto find_pc = dbp_table.find(pc);
        if(find_pc!=dbp_table.end()){
            Ptable *p = &find_pc->second;
            p->writes++;
            if(p->sat >= HMAX)
            {
                p->bypass = true;
            }
            if(p->sat < MAX)
                p->sat++;
        }   
        else{
            dbp_table[pc] = Ptable();
        }
        wr_counter++;
    }

    void upon_access_hit(IntPtr pc){
        auto find_pc = dbp_table.find(pc);
        if(find_pc!=dbp_table.end()){
            Ptable *p = &find_pc->second;
            p->writes++;
            if(p->sat < HMAX)
            {
                p->bypass = false;
            }
            if(p->sat > 0)
                p->sat--;
        }
        wr_counter++;
    }
};

