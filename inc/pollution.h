#ifndef POLLUTION_H
#define POLLUTION_H

#include <bits/stdc++.h>

using namespace std;

enum PollutionTracker
{
    TranslationPollutionTracker=0,
    VictimaPollutionTracker,
    PollutionTracker_end
};

enum EvictCause
{
    DATA_BLOCK_EVICTED_BY_TRANSLATION_BLOCK = 0,
    INVALID_CAUSE = -1,
    EvictCause_end
};

class PollutionEntry
{
    public:
    uint64_t addr;
    // tracking type-of-pollution tracker and Eviction-cause for this cache block address, 
    // since we are maintaining a single global eviction history per set
    // but we have multiple pollution traker using it, 
    // to differentiate a cache block eviction cause we put this vector stroing a pollutiontracker and its cause,
    // allowing us to avoid storing eviction history for each pollution tracker
    vector<pair<PollutionTracker, EvictCause>> cause;
    int thread_id =-1;

    PollutionEntry(){};
    PollutionEntry(uint64_t addr, pair<PollutionTracker, EvictCause> metadata, int thread_id)
    {
        this->addr = addr;
        this->cause.push_back(metadata);
        this->thread_id = thread_id;
    }

    // append pollution tracker and its eviction cause if cache block is already tracked in by other pollution tracker
    void append_cause(const vector<pair<PollutionTracker, EvictCause>> inc_cause)
    {
        for(auto& entry: inc_cause)
        {
            cause.push_back(entry);
        }
    }
};

class Pollution
{
    protected:

    // global eviction history per set
    vector<vector<PollutionEntry>>* ref;

    inline vector<PollutionEntry>& get_history_for_set(int set) {
        return (*ref)[set];
    }

    public:
    int pollution = 0;
    int HIS_LIMIT = 0;
    int num_set;

    Pollution(){}
    Pollution(int sets, int ways, vector<vector<PollutionEntry>>* ref): num_set(sets)
    {
        this->ref = ref;
        HIS_LIMIT = 4*ways;
    }

    void insert(int set, const PollutionEntry& entry)
    {
        vector<PollutionEntry>& set_hist = get_history_for_set(set);
        
        // check if pollution_entry already pushed by some other Pollution Trackers
        for(auto& se: set_hist)
        {
            if(se.addr == entry.addr)
            {
                // if victima, then make sure to match thread_id
                if(entry.cause[0].first == PollutionTracker::VictimaPollutionTracker)
                {
                    // append eviction_cause or insert new entry 
                    if( se.thread_id == entry.thread_id )
                    {
                        se.append_cause(entry.cause);
                    }
                    else
                        break;
                }
                else 
                {
                    se.append_cause(entry.cause);
                }

                return;
            }
        }

        if(set_hist.size() >= HIS_LIMIT)
        {
            set_hist.erase(set_hist.begin());
        }

        set_hist.push_back(entry);
    }

    virtual void countPollution(int set, const PollutionEntry& entry) = 0;
    virtual void print(string tag) = 0;
};

class TranslationPollution: public Pollution
{
    public:

    using Pollution::Pollution;

    TranslationPollution(int sets, int ways, std::vector<std::vector<PollutionEntry>>* ref)
    : Pollution(sets, ways, ref) {}

    void countPollution(int set, const PollutionEntry& entry) override
    {   
        vector<PollutionEntry>& set_hist = this->get_history_for_set(set);
        // set history
        for(PollutionEntry& pe: set_hist)
        {
            // entry found
            if(pe.addr == entry.addr && pe.thread_id == entry.thread_id)
            {
                // test cause
                for(auto& ca: pe.cause)
                {
                    if(ca.second == DATA_BLOCK_EVICTED_BY_TRANSLATION_BLOCK && ca.first == PollutionTracker::TranslationPollutionTracker)
                    {
                        pollution++;
                        return;
                    }
                }
            }
        }
    }

    void print(string tag) override
    {
        std::cout << tag << " Translation induced pollution, " << pollution << '\n';
    }
};

class VictimaPollution: public Pollution
{
    public:

    using Pollution::Pollution;
    
    VictimaPollution(int sets, int ways, std::vector<std::vector<PollutionEntry>>* ref)
    : Pollution(sets, ways, ref) {}

    void countPollution(int set, const PollutionEntry& entry) override
    {   
        std::vector<PollutionEntry>& set_hist = this->get_history_for_set(set);
        // set history
        for(PollutionEntry& pe: set_hist)
        {
            // entry found
            if(pe.addr == entry.addr && pe.thread_id == entry.thread_id)
            {
                // test cause
                for(auto& ca: pe.cause)
                {
                    if(ca.second == DATA_BLOCK_EVICTED_BY_TRANSLATION_BLOCK && ca.first == PollutionTracker::VictimaPollutionTracker)
                    {
                        pollution++;
                        return;
                    }
                }
            }
        }
    }

    void print(string tag) override
    {
        std::cout << tag << " Victima induced pollution, " << pollution << '\n';
    }
};

#endif