#ifndef OFFLINEEDGELIFETIME_H
#define OFFLINEEDGELIFETIME_H

#include <bits/stdc++.h>
#include "log.h"

#define IntPtr uint64_t

using namespace std;

class OfflineEdgeLifetime
{
    public:
    vector<pair<IntPtr, IntPtr>> heavy_edge_map;
    map<string, vector<IntPtr>> edge_lifetime;

    bool found_test_csv;

    OfflineEdgeLifetime()
    {
        string filePath = "./chain/test.csv";
        FILE* in = freopen(filePath.c_str(), "r", stdin);

        if(allow())
        {
            found_test_csv = false;
            printf("[Warning] test.csv stage 2!\n");
        }
        else
        {
            found_test_csv = true;
            IntPtr a,b,c;
            while(cin >> std::hex >> a >> b >> c)
            {
            heavy_edge_map.push_back({a,b});
            }
        }
    }

    bool allow()
    {
        return STAGE==2;
    }

    void func_check(vector<IntPtr> pc, IntPtr cycle)
    {
        if(!found_test_csv) return;

        if(pc.size()<=1) return;

        for(auto e: heavy_edge_map)
        {
            // src pc[0]
            if(pc[0] == e.first)
            {
                // dst among pc[1:]
                for(int i=1; i< pc.size(); i++)
                {
                    if(pc[i] == e.second)
                    {
                        string key = to_string(e.first)+"_"+to_string(e.second);
                        edge_lifetime[key].push_back(cycle);
                        return; 
                    }
                }
            }
        }
    }

    void fun_output()
    {
        if(!found_test_csv) return;

        string s = "edge_life.log";
        fstream out = Log::get_file_stream(s);

        for(auto e: edge_lifetime)
        {
            out << e.first << ',';
            for(auto f: e.second)
            {
                out << f <<',';
            }
            out << '\n';
        }
    }
};

#endif