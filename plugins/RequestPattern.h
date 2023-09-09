#ifndef REQUEST_PATTERN_H
#define REQUEST_PATTERN_H

#include "../inc/log.h"
#include <bits/stdc++.h>

using namespace std;

class Pattern
{
    public:
    map<string, uint32_t> req_seq;
    Pattern(){}

    void insert(string pat)
    {
        if(req_seq.find(pat)!=req_seq.end())
        {
            req_seq[pat]++;
        }
        else
        {
            req_seq.insert({pat, 1});
        }
    }
};

class RequestPattern
{
    public:
    vector<Pattern> *pattern;
    Pattern* set_req_pattern;

    int* mapfunc;
    int num_ways;
    int num_sets;
    int SAMPLE_SET=1024;

    RequestPattern(){}
    ~RequestPattern(){}

    /*num_ways, num_sets*/
    RequestPattern(uint32_t num_ways, uint32_t num_sets)
    {   
        this->num_ways = num_ways;
        this->num_sets = num_sets;

        mapfunc = (int*)calloc(num_sets, sizeof(int));

        pattern = new vector<Pattern>[num_sets];

        for(uint32_t i=0; i< num_sets; i++)
            pattern[i] = vector<Pattern>(num_ways);
        
        for(uint32_t i=0; i< SAMPLE_SET; i++)
        {
            uint32_t sample_set = rand()%num_sets;

            // if we already have sample_set mapped, then try different
            while(mapfunc[sample_set]!=0)
            {
                sample_set = rand()%num_sets;
            }

            // map sample_set to 1
            mapfunc[sample_set] = 1;
        }

        set_req_pattern = new Pattern[num_sets];
    }

    bool func_is_sampled(uint32_t set_no)
    {
        if(mapfunc[set_no]==0)
            return false;
        return true;
    }

    void func_evict_event(uint32_t way_no, uint32_t set_no, string pat)
    {
        uint32_t valid = mapfunc[set_no];
        if(valid==0)
            return;

        pattern[set_no][way_no].insert(pat);   
    }

    void func_print()
    {
        string s = "llc_req_pattern.log";
        fstream f = Log::get_file_stream(s);
        for(int i=0; i< num_sets; i++)
        {
            if(mapfunc[i]==0)
                continue;

            // pattern on each block of corresponding set i
            vector<Pattern> data = pattern[i];

            for(int j=0; j< num_ways; j++)
            {
                for(auto u : data[j].req_seq)
                {
                    f << i << "," << j << "," << u.second << "," << u.first << '\n';
                }
            }

        }
    }
};
#endif