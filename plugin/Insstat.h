#ifndef INSSTAT_H
#define INSSTAT_H

#include <bits/stdc++.h>
#include "log.h"
using namespace std;

typedef uint64_t IntPtr;
#define CONTEXT 4
#define ACCESS_TYPE 3
class InsStat
{
    public:

    

    string name;

    /* count loads/stores inctructions*/
    IntPtr ldst[ACCESS_TYPE]={0};

    /*count context (1,2,3) based load/store accesses*/
    IntPtr context_based_accesses[CONTEXT][ACCESS_TYPE];

    InsStat(string name){
        for(int i=0; i< CONTEXT; i++)
        {
            for(int j=0; j< ACCESS_TYPE; j++)
            {
                context_based_accesses[i][j] = 0;
            }
        }
        this->name = name;
    }
    ~InsStat(){
        func_print();
    }

    
    //  * @brief increase load/store count by type. total as well
    //  * 
    //  * @param context index (1), edge (2), property (3)
    //  * @param what  ld hit 0, st hit 1, fillback (ld miss/ st miss) 2
    void func_increase_by_type(int context, int what)
    {
        if(context >= CONTEXT || what >= ACCESS_TYPE)
        {
            printf("Not allowd\n");
            exit(0);
        }
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