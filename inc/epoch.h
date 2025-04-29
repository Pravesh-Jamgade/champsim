#ifndef EPOC_H
#define EPOC_H
#include <bits/stdc++.h>
#define LL long long
using namespace std;
class Epoc
{
    LL epoc_number;
    LL epoc_size;
    LL prev_epoc_start_cycle;
    public:
    Epoc()
    {
        epoc_number = 0;
        epoc_size = 10000000;
        prev_epoc_start_cycle = 0;
    }

    // epoc ended
    bool test(LL current_cycle)
    {
        int diff = current_cycle - prev_epoc_start_cycle;
        if(diff > epoc_size)
        {
            epoc_number++;
            prev_epoc_start_cycle = current_cycle;
            return true;
        }
        return false;
    }
};

#endif 