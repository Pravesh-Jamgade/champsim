#ifndef CACHE_H
#define CACHE_H

#include "memory_class.h"
#include<bits/stdc++.h>
extern uint32_t pdegree;
// PAGE
extern uint32_t PAGE_TABLE_LATENCY, SWAP_LATENCY;
// //---------------------DK start----------------------------------//
// #ifdef PREFETCH_INCLUDED 
//  extern uint32_t pdegree;  // Value if prefetcher is included
// #else
//   uint32_t pdegree = 0;   // Default value if prefetcher is not included
// #endif
// //---------------------DK end-----------------------------------//
// CACHE TYPE
#define IS_ITLB 0
#define IS_DTLB 1
#define IS_STLB 2
#define IS_L1I  3
#define IS_L1D  4
#define IS_L2C  5
#define IS_LLC  6

// INSTRUCTION TLB
#define ITLB_SET 16
#define ITLB_WAY 4
#define ITLB_RQ_SIZE 16
#define ITLB_WQ_SIZE 16
#define ITLB_PQ_SIZE 0
#define ITLB_MSHR_SIZE 8
#define ITLB_LATENCY 1

//Tag Array
#define L1DTAG_LATENCY 2
#define L1ITAG_LATENCY 2    //Dikshit
#define L2TAG_LATENCY 4
#define L3TAG_LATENCY 6

// DATA TLB
#define DTLB_SET 16
#define DTLB_WAY 4
#define DTLB_RQ_SIZE 16
#define DTLB_WQ_SIZE 16
#define DTLB_PQ_SIZE 0
#define DTLB_MSHR_SIZE 8
#define DTLB_LATENCY 1

// SECOND LEVEL TLB
#define STLB_SET 128
#define STLB_WAY 12
#define STLB_RQ_SIZE 32
#define STLB_WQ_SIZE 32
#define STLB_PQ_SIZE 0
#define STLB_MSHR_SIZE 16
#define STLB_LATENCY 8

// L1 INSTRUCTION CACHE
#define L1I_SET 64
#define L1I_WAY 8
#define L1I_RQ_SIZE 64
#define L1I_WQ_SIZE 64 
#define L1I_PQ_SIZE 32
#define L1I_MSHR_SIZE 8
#define L1I_LATENCY 4//+L1ITAG_LATENCY

// L1 DATA CACHE
#define L1D_SET 64
#define L1D_WAY 12
#define L1D_RQ_SIZE 64
#define L1D_WQ_SIZE 64 
#define L1D_PQ_SIZE 8
#define L1D_MSHR_SIZE 16
#define L1D_LATENCY 5//+L1DTAG_LATENCY

// L2 CACHE
#define L2C_SET 1024
#define L2C_WAY 8
#define L2C_RQ_SIZE 32
#define L2C_WQ_SIZE 32
#define L2C_PQ_SIZE 16
#define L2C_MSHR_SIZE 32
#define L2C_LATENCY 10//+L2TAG_LATENCY  // 4/5 (L1I or L1D) + 10 = 14/15 cycles

// LAST LEVEL CACHE
#define LLC_SET NUM_CPUS*2048
#define LLC_WAY 16
#define LLC_RQ_SIZE NUM_CPUS*L2C_MSHR_SIZE //48
#define LLC_WQ_SIZE NUM_CPUS*L2C_MSHR_SIZE //48
#define LLC_PQ_SIZE NUM_CPUS*32
#define LLC_MSHR_SIZE NUM_CPUS*64
#define LLC_LATENCY 20//+L3TAG_LATENCY  // 4/5 (L1I or L1D) + 10 + 20 = 34/35 cycles

#define persetpfusefull 0 //DK
#define persetpfuseless 1 //DK
#define persetpfaccuracy 2 //DK

//extern uint32_t pdegree; //DK
class CACHE : public MEMORY {
  public:
    uint32_t cpu;
    const string NAME;
    const uint32_t NUM_SET, NUM_WAY, NUM_LINE, WQ_SIZE, RQ_SIZE, PQ_SIZE, MSHR_SIZE;
    uint32_t LATENCY;
    BLOCK **block;
    int fill_level;
    uint32_t MAX_READ, MAX_FILL;
    uint32_t reads_available_this_cycle;
    uint8_t cache_type;
    //vector<int> MSHR_OCC(MSHR_SIZE, 0);
    int MSHR_OCC[LLC_MSHR_SIZE];//DK intialize at line 168
    uint64_t Out_Of_MSHR;
 

    // prefetch stats
    uint64_t pf_requested,
             pf_issued,
             pf_useful,
             pf_useless,
             pf_fill,
             pfCount=0,//DK
             perSetAccuracy[LLC_SET][3]; //DK "For Perset accuracy"


    // queues
    PACKET_QUEUE WQ{NAME + "_WQ", WQ_SIZE}, // write queue
                 RQ{NAME + "_RQ", RQ_SIZE}, // read queue
                 PQ{NAME + "_PQ", PQ_SIZE}, // prefetch queue   /*object of class PACKET_QUEUE which is in the block.h file*/
                 MSHR{NAME + "_MSHR", MSHR_SIZE}, // MSHR
                 PROCESSED{NAME + "_PROCESSED", ROB_SIZE}; // processed queue

    uint64_t sim_access[NUM_CPUS][NUM_TYPES],
             sim_hit[NUM_CPUS][NUM_TYPES],
             sim_miss[NUM_CPUS][NUM_TYPES],
             roi_access[NUM_CPUS][NUM_TYPES],
             roi_hit[NUM_CPUS][NUM_TYPES],
             roi_miss[NUM_CPUS][NUM_TYPES];

    uint64_t total_miss_latency;
    
    // constructor
    CACHE(string v1, uint32_t v2, int v3, uint32_t v4, uint32_t v5, uint32_t v6, uint32_t v7, uint32_t v8) 
        : NAME(v1), NUM_SET(v2), NUM_WAY(v3), NUM_LINE(v4), WQ_SIZE(v5), RQ_SIZE(v6), PQ_SIZE(v7), MSHR_SIZE(v8) {

        LATENCY = 0;

        // cache block
        block = new BLOCK* [NUM_SET];
        for (uint32_t i=0; i<NUM_SET; i++) {
            block[i] = new BLOCK[NUM_WAY]; 

            for (uint32_t j=0; j<NUM_WAY; j++) {
                block[i][j].lru = j;
            }
        }

        for (uint32_t i=0; i<NUM_CPUS; i++) {
            upper_level_icache[i] = NULL;
            upper_level_dcache[i] = NULL;

            for (uint32_t j=0; j<NUM_TYPES; j++) {
                sim_access[i][j] = 0;
                sim_hit[i][j] = 0;
                sim_miss[i][j] = 0;
                roi_access[i][j] = 0;
                roi_hit[i][j] = 0;
                roi_miss[i][j] = 0;
            }
        }

	total_miss_latency = 0;

        lower_level = NULL;
        extra_interface = NULL;
        fill_level = -1;
        MAX_READ = 1;
        MAX_FILL = 1;

        pf_requested = 0;
        pf_issued = 0;
        pf_useful = 0;
        pf_useless = 0;
        pf_fill = 0;
        //pdegree=0;//DK
        Out_Of_MSHR=0;
        
        for(int i=0;i<LLC_MSHR_SIZE;i++)
        {
            MSHR_OCC[i]=0;
        }

        for(int i=0;i<LLC_MSHR_SIZE;i++)
        {
            perSetAccuracy[i][persetpfusefull]=0; //usefulness value for that set.
            perSetAccuracy[i][persetpfuseless]=0; //total prefetch request sent to memory for set i.
            perSetAccuracy[i][persetpfaccuracy]=0;
        }

        //DK
       
    };

    // destructor
    ~CACHE() {
    //[D=============================================================
        printMshr_state();
    //K]=============================================================
        //cout<<"MSHR_Ossupancy_test"; //DK
        for (uint32_t i=0; i<NUM_SET; i++)
            delete[] block[i];
        delete[] block;

    };
    //[D==============================================
    float summ(int arr[],int Csize)
    {
        float sum=0;
        for(int i=0;i<Csize;i++)
        {
            sum=sum+arr[i];
        }
        return sum;
    }
    //K]===============================================

    // functions
    int  add_rq(PACKET *packet),  /*add_rq add_wq add_pq all three functions are defined in cache.cc file*/
         add_wq(PACKET *packet),
         add_pq(PACKET *packet);

    void return_data(PACKET *packet),  //defined in cache.cc file
         operate(), //defind in cache.cc file.
         increment_WQ_FULL(uint64_t address);

    uint32_t get_occupancy(uint8_t queue_type, uint64_t address), // Both function defined cache.cc file.
             get_size(uint8_t queue_type, uint64_t address);

    int  check_hit(PACKET *packet),
         invalidate_entry(uint64_t inval_addr),
         check_mshr(PACKET *packet),  //defined in cache.cc file.
         prefetch_line(uint64_t ip, uint64_t base_addr, uint64_t pf_addr, int prefetch_fill_level, uint32_t prefetch_metadata),
         kpc_prefetch_line(uint64_t base_addr, uint64_t pf_addr, int prefetch_fill_level, int delta, int depth, int signature, int confidence, uint32_t prefetch_metadata);

    void handle_fill(),
         handle_writeback(),   //function definition in cache.cc file.
         handle_read(),
         handle_prefetch();

    void add_mshr(PACKET *packet), //----add_mshr is defined in cache.cc file.
         update_fill_cycle(), //defined in cache.cc file.
         llc_initialize_replacement(),
         update_replacement_state(uint32_t cpu, uint32_t set, uint32_t way, uint64_t full_addr, uint64_t ip, uint64_t victim_addr, uint32_t type, uint8_t hit),
         llc_update_replacement_state(uint32_t cpu, uint32_t set, uint32_t way, uint64_t full_addr, uint64_t ip, uint64_t victim_addr, uint32_t type, uint8_t hit);
        //-------------------------------------DK--------------------------//
    void mshrOcc_track(),printMshr_state(),
         llc_IPVpromotion(uint32_t IPV[], uint32_t set, uint32_t way),//DK
         llc_IPVinsertion(uint32_t IPV[],uint32_t set, uint32_t way),//DK
         llc_Epromotion(uint32_t IPV[][LLC_WAY+1],uint32_t set,uint32_t way,uint8_t tp),//DK
         llc_Einsertion(uint32_t IPV[][LLC_WAY+1],uint32_t set,uint32_t way,uint8_t tp),//DK
         llc_resetpfstats(uint32_t set);//DK
    uint32_t minPrefetchlru(uint32_t set,int tmin);//DK
    int PFblockCount(uint32_t set);//DK
    vector<pair<uint32_t,uint32_t>>CollectPfBlock(uint32_t set);
        //-------------------------------------DK--------------------------//
    void lru_update(uint32_t set, uint32_t way),
         fill_cache(uint32_t set, uint32_t way, PACKET *packet),  //Defined in cache.cc file.
         replacement_final_stats(),
         llc_replacement_final_stats(),
        //prefetcher_initialize(),
         l1d_prefetcher_initialize(),
         l2c_prefetcher_initialize(),
         llc_prefetcher_initialize(),
         prefetcher_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type),
         l1d_prefetcher_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type),
         prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr),
         l1d_prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in),
         //prefetcher_final_stats(),
         l1d_prefetcher_final_stats(),
         l2c_prefetcher_final_stats(),
         llc_prefetcher_final_stats();
    void (*l1i_prefetcher_cache_operate)(uint32_t, uint64_t, uint8_t, uint8_t);
    void (*l1i_prefetcher_cache_fill)(uint32_t, uint64_t, uint32_t, uint32_t, uint8_t, uint64_t);
//prefetcher related functions
    uint32_t l2c_prefetcher_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in),
         llc_prefetcher_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in),
         l2c_prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in),
         llc_prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in);
//replacement related function
    uint32_t get_set(uint64_t address),  //used in cache.cc
             get_way(uint64_t address, uint32_t set),
             find_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type),
             llc_find_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type),
             lru_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type);
};

#endif
