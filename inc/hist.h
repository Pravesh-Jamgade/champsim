#ifndef HIST_H
#define HIST_H
#include<bits/stdc++.h>
#include <iomanip>
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
        // reset histogram buckets before accumulating data frequencies
        std::fill(hist_distance.begin(), hist_distance.end(), 0);

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

    template <typename Container>
    void print_histogram_matrix(const string& tag, const vector<string>& column_labels, const Container& data_by_column)
    {
        if (hits_bounds.empty() || column_labels.empty())
            return;

        vector<vector<int>> bucket_by_column(column_labels.size(), vector<int>(hits_bounds.size(), 0));

        for (size_t col = 0; col < column_labels.size(); ++col)
        {
            for (const auto& data : data_by_column[col])
            {
                for (size_t bucket = 0; bucket < hits_bounds.size(); ++bucket)
                {
                    auto bound = hits_bounds[bucket];
                    if (bound.first <= data.first && data.first <= bound.second)
                    {
                        bucket_by_column[col][bucket] += data.second;
                    }
                }
            }
        }

        cout << tag << " histogram bucket-by-type\n";
        cout << left << setw(18) << "Bucket";
        for (const auto& label : column_labels)
            cout << setw(12) << label;
        cout << '\n';

        for (size_t bucket = 0; bucket < hits_bounds.size(); ++bucket)
        {
            auto bound = hits_bounds[bucket];
            cout << left << setw(18) << (to_string(bound.first) + " - " + to_string(bound.second));
            for (size_t col = 0; col < column_labels.size(); ++col)
                cout << setw(12) << bucket_by_column[col][bucket];
            cout << '\n';
        }
        cout << '\n';
    }
};


#endif