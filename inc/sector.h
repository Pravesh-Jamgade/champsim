#ifndef SECTOR_H
#define SECTOR_H

#include <bits/stdc++.h>
#include <cstdint>
#include "util.h"
#include <variant>
using namespace std;

extern uint64_t KNOB_SUBTAG_BITS;

struct LookupResultU64 { bool hit; uint64_t value; };

struct Indexer
{
    static int get_num_bits(uint64_t num) {return num%2==0 ? lg2(num): 1+lg2(num); }
    static int get_mask(uint64_t num_bits) {return pow(2, num_bits) - 1;}
    // page address
    static int get_index(uint64_t addr) { return (addr & 0x7ULL);}
    // page address, no_of_bits_in_set
    static int get_subTag(uint64_t addr, int set_num) { return ( (addr >> (3+get_num_bits(set_num))) & get_mask(KNOB_SUBTAG_BITS) ); }
};

struct DirectMap
{
    struct Entry
    { 
        bool valid=false; 
        uint64_t pte=0;
        uint64_t subTag=0;
    };

    array<Entry, 8> slots{};
    
    // page address
    LookupResultU64 lookup(uint64_t page_addr, int NUM_SET)
    {
        int index = Indexer::get_index(page_addr);
        uint64_t subTag = Indexer::get_subTag(page_addr, NUM_SET);
        if(slots[index].valid && slots[index].subTag==subTag)
        {
            return {true, slots[index].pte};
        }
        return {false, 0};
    }

    // update at index even if it is valid
    void insert(uint64_t page_addr, uint64_t pte, int NUM_SET)
    {
        int index = Indexer::get_index(page_addr);
        slots[index].valid = true;
        slots[index].pte = pte;
        slots[index].subTag = Indexer::get_subTag(page_addr, NUM_SET);
    }

    // void dump() {dumper::dump(slots);}
    void dump()
    {
        for(int i=0; i< slots.size(); i++)
            cout << "i="<<i <<", "<< slots[i].pte << ", " << slots[i].subTag << '\n';
    }
};

struct LRU8
{
    int timeTick = 0;

    template<class EntryArr>
    int victim(const EntryArr& ent)
    {
        // Prefer an invalid slot first
        for (int i = 0; i < 8; ++i) if (!ent[i].valid) return i;
        // Otherwise least-recently used
        uint64_t bestAge = std::numeric_limits<uint64_t>::max();
        int bestIdx = 0;
        for (int i = 0; i < 8; ++i) {
            if (ent[i].age < bestAge) { bestAge = ent[i].age; bestIdx = i; }
        }
        return bestIdx;
    }

    template<class EntryArr>
    void insert(EntryArr& slots, uint64_t subTag, uint64_t pte)
    {
        timeTick++;
        int index = victim(slots);
        slots[index].subTag = subTag;
        slots[index].pte = pte;
        slots[index].age = timeTick;
        slots[index].valid = true;
        cout << "id, " << index << ", subTag, " << subTag << ", pte, " << pte << ", timeTick, " << timeTick << '\n';
    }

    template<class EntryArr>
    LookupResultU64 lookup(EntryArr& slots, uint64_t subTag)
    {
        for(int i=0; i< 8; i++)
        {
            if(slots[i].valid && slots[i].subTag == subTag)
            {
                timeTick++;
                slots[i].age = timeTick;
                return {true, slots[i].pte};
            }
        }
        return {false, 0};
    }
};

// default parameterized for LRU8
template<class ReplacementManager=LRU8>
struct AssociativeMap
{
    struct Entry
    {
        bool valid=false;
        uint64_t pte=0;
        uint64_t subTag=0;
        uint64_t age=0;
    };

    int timeTick=0;
    array<Entry, 8> slots;
    ReplacementManager    repl; 

    LookupResultU64 lookup(uint64_t page_addr, int NUM_SET)
    {
        uint64_t subTag = Indexer::get_subTag(page_addr, NUM_SET);
        uint64_t pte_offset = Indexer::get_index(page_addr);
        uint64_t combinedTag = (subTag << 3) | pte_offset;
        return repl.lookup(slots, combinedTag);
    }

    void insert(uint64_t page_addr, uint64_t pte, int NUM_SET)
    {
        uint64_t subTag = Indexer::get_subTag(page_addr, NUM_SET);
        uint64_t pte_offset = Indexer::get_index(page_addr);
        uint64_t combinedTag = (subTag << 3) | pte_offset;
        repl.insert(slots, combinedTag, pte);
    }

    // void dump() {dumper::dump(slots);}
    void dump()
    {
        for(int i=0; i< slots.size(); i++)
            cout << "i="<<i <<", "<< slots[i].pte << ", " << slots[i].subTag << ", valid, " << slots[i].valid << '\n';
    }
};

// compile time selection helper
enum class SectorDesingChoice {DIRECT=0, ASSOCIATIVE};

// primary selector
template<SectorDesingChoice C>
struct SectorDesignSelector;

// explicit specialization
template<>
struct SectorDesignSelector<SectorDesingChoice::DIRECT>
{
    using type = DirectMap;
};

template<>
struct SectorDesignSelector<SectorDesingChoice::ASSOCIATIVE>
{
    using type = AssociativeMap<LRU8>;
};

// ---- Type-erased interface so callers can use one uniform API ----
struct ISector {
    virtual ~ISector() {}
    virtual LookupResultU64 lookup(uint64_t page_addr, int log2_sets) = 0;
    virtual void            insert(uint64_t page_addr, uint64_t pte, int log2_sets) = 0;
    virtual void            dump() = 0;
};

// Model adapter for any concrete sector that implements the same API
template <class Impl>
struct SectorModel : ISector {
    Impl impl;
    std::unique_ptr<ISector> clone() const override {
        return std::unique_ptr<ISector>(new SectorModel<Impl>(*this)); // copies Impl
      }
    LookupResultU64 lookup(uint64_t page_addr, int log2_sets) override {
        return impl.lookup(page_addr, log2_sets);
    }
    void insert(uint64_t page_addr, uint64_t pte, int log2_sets) override {
        impl.insert(page_addr, pte, log2_sets);
    }
    void dump() override { impl.dump(); }

    // Optional: expose raw impl if you ever need it
    Impl&       raw()       { return impl; }
    const Impl& raw() const { return impl; }
};

class SectorHolder 
{
    public:
      using Sector = std::variant<DirectMap, AssociativeMap<LRU8>>;

      // Design choice and is_it_sector_cache_way [deafaul false]
      static SectorHolder make(SectorDesingChoice c, bool isSector=false) {
        SectorHolder h;
        if (c == SectorDesingChoice::ASSOCIATIVE) h.sector_ = AssociativeMap<LRU8>{};
        else                                      h.sector_ = DirectMap{};
        h.choice_ = c;
        h.is_sector_line = isSector;
        return h;                       // copyable/movable
      }
    
      LookupResultU64 lookup(uint64_t addr, int log2_sets) {
        return std::visit([&](auto& s){ return s.lookup(addr, log2_sets); }, sector_);
      }
      void insert(uint64_t addr, uint64_t pte, int log2_sets) {
        std::visit([&](auto& s){ s.insert(addr, pte, log2_sets); }, sector_);
      }
      void dump() {
        std::visit([&](auto & s){ s.dump(); }, sector_);
      }
    
      SectorDesingChoice choice() const { return choice_; }
      
      bool is_sector_line = false;
    private:
      SectorDesingChoice choice_ = SectorDesingChoice::DIRECT;
      Sector sector_ = DirectMap{};     // default to direct
};

#endif // SECTOR_H
