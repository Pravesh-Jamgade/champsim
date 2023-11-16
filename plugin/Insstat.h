#ifndef INSSTAT_H
#define INSSTAT_H

#include <bits/stdc++.h>
#include "log.h"
using namespace std;

typedef uint64_t IntPtr;

class InsStat
{
    public:

    int CONTEXT = 4;
    int ACCESS_TYPE = 4;

    string name;

    /* count loads/stores inctructions*/
    IntPtr ldst[2]={0};

    /*count context (1,2,3) based load/store accesses*/
    IntPtr** context_based_accesses;

    InsStat(string name){
        context_based_accesses = (IntPtr**)malloc(sizeof(IntPtr)*CONTEXT);
        for(int i=0; i< CONTEXT; i++)
        {
            context_based_accesses[i] = (IntPtr*)malloc(sizeof(IntPtr)*ACCESS_TYPE);
        }
        this->name = name;
    }
    ~InsStat(){
        func_print();
    }

    
    //
    /**
     * @brief increase load/store count by type. total as well
     * 
     * @param context index (1), edge (2), property (3)
     * @param what load 0/rfo(store) 1/ prefetch(curr disabled) 2/ writeback 3/ fill = 0/1 
     */
    void func_increase_by_type(int context, int what)
    {
        ldst[what]+=1;
        context_based_accesses[context][what] += 1;
    }


    void func_print()
    {
        string s = "context_ldst.log";
        fstream a = Log::get_file_stream(s);

        string type_name[CONTEXT] = {"other", "index", "edge", "property"};

        for(int i=0; i< CONTEXT; i++)
        {
            string ldst_count ="";
            for(int j=0; j< ACCESS_TYPE; j++)
            {
                ldst_count = ldst_count + to_string(context_based_accesses[i][j]) + ", ";
            }
            a << name << ", " << type_name[i] << ", " <<  ldst << '\n';
        }

        a << name << ", " << "ldst, " << to_string(ldst[0]) << ", " << to_string(ldst[1]) << ", " << '\n'; 
        a.close();
        
    }
};

#endif