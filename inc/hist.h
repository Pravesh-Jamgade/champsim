#ifndef HIST_H
#define HIST_H
#include<bits/stdc++.h>
using namespace std;

class Hist
{
    public:
    // data and frequency
    map<int,int> data_freq;
    // bucket bounds
    vector<pair<int,int>> hits_bounds;
    // count bucket_bound frequncy
    vector<int> hist_distance;

    Hist(){}
    Hist(int start, int width, int count, vector<pair<int,int>>& exceptional_boundries)
    {
        for(int i=0; i< count; i++)
        {
            hits_bounds.push_back({start, start+width});
            start += width+1;
        }

        for(auto entry: exceptional_boundries)
            hits_bounds.push_back(entry);
        
        hist_distance.resize(hits_bounds.size(), 0);
    }

    void add_data_freq(int data, int freq){
        data_freq[data] += freq;
    }

    void custom_add_hist_bounds(vector<pair<int,int>>& bounds){
        hits_bounds = bounds;
        hist_distance.resize(hits_bounds.size(), 0);
    }

    void print_histogram(string tag)
    {
        // data is reuse_distance and its corresponding frequecny
        for(auto data: data_freq)
        {
            // look for bounds to which this reuse distance belongs to
            for(int i=0; i< hits_bounds.size(); i++)
            {
                pair<int,int> bound = hits_bounds[i];

                // if data is within bucket_boundry, sumup its frequcny in final histogram
                if(bound.first <= data.first && data.first <= bound.second)
                {
                    // i'th bucket of histogram
                    hist_distance[i] += data.second;
                }
            }
        }
        // print histogram
        for(int i=0; i< hits_bounds.size(); i++)
        {
            pair<int,int> bound = hits_bounds[i];
            cout << bound.first << " - " << bound.second << ", " <<  hist_distance[i] << '\n';
        }
        cout << '\n';
    }
};


#endif