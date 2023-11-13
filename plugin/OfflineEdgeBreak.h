#ifndef OFFLINEEDGEBREAK_H
#define OFFLINEEDGEBREAK_H

#include <bits/stdc++.h>
#define IntPtr uint64_t

using namespace std;

class OfflineEdgeBreak
{
    public:

    bool found_test_csv;
    set<string> heavy_edge_map;

    OfflineEdgeBreak()
    {
        string filePath = "./chain/test.csv";
        FILE* in = freopen(filePath.c_str(), "r", stdin);

        /*
        TODO:
            we could have done these step in stage - 2 only, when we are considering freq of edge as the key dtermining factor.
            But when we want to use lifetime of edge then we should do stage == 3 and run post processing thrid time.

            Check whether top strong edges, are consistent with their lifetime across number of different workloads.
            If there is relationship between strong edges and lifetime, 
            then we dont need stage 3, as we no longer need lifetime information along with top freq based edges. 
            

            currently i will use freq of edge only to understand edge breaking i.e. stage 2
        */
        if(!allow())
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
                heavy_edge_map.insert(to_string(a) + '_' + to_string(b));
            }
        }
    }

    bool allow()
    {
        return STAGE==2;
    }

    void func_on_evict(vector<IntPtr>& chain)
    {
        if(!allow()) return;

        bool found_producer = false;

        string base = to_string(chain[0]);

        for(int i=1; i< chain.size(); i++)
        {
            string edge = base + '_' + to_string(chain[i]);

            auto findEdge = heavy_edge_map.find(edge);
            
            if(findEdge!= heavy_edge_map.end())
            {

            }
        }

    }
};

#endif