// dead block prediction module

#include <iostream>
#include <map>


#define IntPtr uint64_t
#define MAX 3
#define HMAX 2

using namespace std;

class Ptable{
    public:
    int sat;
    bool bypass;
    Ptable(){sat=bypass=0;}

};

class DBPModule
{
    private:
    public:

    map<IntPtr, Ptable> dbp_table;

    
    bool lookup(IntPtr pc){
        auto find_pc = dbp_table.find(pc);
        if(find_pc!=dbp_table.end()){
            return find_pc->second.bypass;
        }
        return false;
    }

    // upon access miss
    void upon_access_miss(IntPtr pc){
        auto find_pc = dbp_table.find(pc);
        if(find_pc!=dbp_table.end()){
            Ptable *p = &find_pc->second;
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
    }

    void upon_access_hit(IntPtr pc){
        auto find_pc = dbp_table.find(pc);
        if(find_pc!=dbp_table.end()){
            Ptable *p = &find_pc->second;
            if(p->sat < HMAX)
            {
                p->bypass = false;
            }
            if(p->sat > 0)
                p->sat--;
        }
    }

};
