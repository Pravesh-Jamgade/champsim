#ifndef OFFLINEEDGELIFETIME_H
#define OFFLINEEDGELIFETIME_H

#include <bits/stdc++.h>
#include "log.h"

#define IntPtr uint64_t

using namespace std;


class EdgeSrc
{
    public:
    set<IntPtr> sources;
};

class OfflineEdgeLifetime
{
    public:
    map<string, vector<IntPtr>> edge_lifetime;
    
    OfflineEdgeLifetime(){}

    void func_track_cycle(IntPtr src, IntPtr dst, IntPtr cycle)
    {
       string edge = std::to_string(src) + "_" + std::to_string(dst);
       auto found = edge_lifetime.find(edge);
       if(found == edge_lifetime.end())
       {
        edge_lifetime[edge] = vector<IntPtr>();
       }
       edge_lifetime[edge].push_back(cycle);
    }

    void fun_output()
    {

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