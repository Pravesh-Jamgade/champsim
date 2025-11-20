#ifndef __DRAMSIM3__H
#define __DRAMSIM3__H

#include "champsim_constants.h"
#include "memory_class.h"
#include "operable.h"
#include "dramsim3.h"
#include "util.h"
#include "pagetable.h"
#include <vector>
#include "vmem.h"
#include "logger.h"
#include "pomtlb.h"
#include "pagemetadata.h"
#include "backtracklog.h"

extern int KNOB_SMT_ENABLE;
extern int KNOB_POMTLB;
extern ProcessPageTable* process_page_table;
extern POMTLB* pomtlb;
extern map<tuple<uint64_t, int>, TblockMetaData> tblockmetadata_tracker;
extern BacktrackLog backtracklog;

// tuple[POM_PP, VP, thread_id] and PP
namespace dramsim3 {
    class MemorySystem;
};

extern uint8_t all_warmup_complete;

// This is a wrapper so DRAMSim (which only returns trans. addr) can communicate
// with ChampSim API (which requires explicit packet->to_return->return_data calls)
// We maintain a "Meta-RQ" to send callbacks to LLC (relies on LLC MSHR to merge duplicate reqs)
class DRAMSim3_DRAM: public champsim::operable, public MemoryRequestConsumer
{
public:
    DRAMSim3_DRAM(double freq_scale, const std::string& config_file, const std::string& output_dir):
        champsim::operable(freq_scale), 
        MemoryRequestConsumer(std::numeric_limits<unsigned>::max()) {
            memory_system_ = new dramsim3::MemorySystem(config_file, output_dir,
                                            std::bind(&DRAMSim3_DRAM::ReadCallBack, this, std::placeholders::_1),
                                            std::bind(&DRAMSim3_DRAM::WriteCallBack, this, std::placeholders::_1));
            std::cout << "DRAMSim3_DRAM init -- fixed meta-RQ size" << std::endl;   
            // memory_system_->RegisterACTCallback(std::bind(&DRAMSim3_DRAM::ACTCallBack, this, 
            //                                     std::placeholders::_1, std::placeholders::_2, 
            //                                     std::placeholders::_3, std::placeholders::_4));
            numPPages = (DRAM_CHANNELS * DRAM_RANKS * DRAM_BANKS 
                                * DRAM_ROWS * DRAM_COLUMNS * BLOCK_SIZE) / PAGE_SIZE;
            procPageAccess = new bool[numPPages]{false};
            dlog = logger(false);
            xlog = logger(false);
            data_page = pt_page = 0;
            data_page_faulted = pt_page_faulted = 0;
        }

    void* getObject(){return this;}
    
    int add_rq(PACKET* packet) override 
    {
        if (all_warmup_complete <= NUM_CPUS) 
        {   
            // should we record vp and pp
            bool should_record = false;

            uint32_t cpu_no = KNOB_SMT_ENABLE*packet->cpu + packet->thread_id;
            if(KNOB_POMTLB && packet->pomflag[POM::POM] && !packet->pomflag[POM::POM_TO_PTW])
            {
                dlog.log(current_cycle, "DRAM-POM-Request, level", (int)packet->translation_level, intToHex(packet->address), intToHex(packet->v_address), intToHex(packet->data), "pom", packet->pomflag[POM::POM], "pommiss", packet->pomflag[POM::POM_MISS], "pomtoptw", packet->pomflag[POM::POM_TO_PTW], '\n');
                backtracklog.track(current_cycle, "DRAM-POM-Request, level", (int)packet->translation_level, intToHex(packet->address), intToHex(packet->v_address), intToHex(packet->data), "pom", packet->pomflag[POM::POM], "pommiss", packet->pomflag[POM::POM_MISS], "pomtoptw", packet->pomflag[POM::POM_TO_PTW], '\n');

                pair<bool, uint64_t> result = pomtlb->lookupPOMEntry(cpu_no, packet->address, packet->v_address);
                pair<bool, vector<pair<bool, PTEHolder>>> pomtlb_line = pomtlb->getPOMTLBLine(cpu_no, packet->address, packet->v_address);
                
                packet->page_fault = true;// its a miss rather than page-fault
                packet->pomflag[POM::POM_MISS] = true;
                if(result.first) // hit in POM-TLB
                {
                    should_record = true;
                    packet->hit_where = CACHE_ID::IS_DRAM;
                    packet->data = result.second;
                    packet->page_fault = false;
                    packet->pomflag[POM::POM_MISS] = false;

                    packet->page_table_entries = pomtlb_line.second;
                    dlog.log(current_cycle, "DRAM-POM-HIT, level", (int)packet->translation_level, intToHex(packet->address), intToHex(packet->v_address), intToHex(packet->data), "pom", packet->pomflag[POM::POM], "pommiss", packet->pomflag[POM::POM_MISS], "pomtoptw", packet->pomflag[POM::POM_TO_PTW], '\n');
                    backtracklog.track(current_cycle, "DRAM-POM-HIT, level", (int)packet->translation_level, intToHex(packet->address), intToHex(packet->v_address), intToHex(packet->data), "pom", packet->pomflag[POM::POM], "pommiss", packet->pomflag[POM::POM_MISS], "pomtoptw", packet->pomflag[POM::POM_TO_PTW], '\n');
                }
            }
            else if(packet->type == TRANSLATION)
            {
                should_record = true;
                pair<bool, PTEHolder> result = process_page_table->operate_pagetable(cpu_no, packet->address, packet->translation_level, packet->v_address);
                pair<int, vector<pair<bool, PTEHolder>>> pt_cache_block = process_page_table->get_cacheblock_data(cpu_no, packet->address, packet->translation_level);
                
                result.first? 0:pt_page_faulted++;
                packet->data = result.second.page_address;
                packet->page_fault = !result.first;
                packet->hit_where = CACHE_ID::IS_DRAM;
                packet->page_table_entries = pt_cache_block.second;

                if(packet->pomflag[POM::POM_TO_PTW] && KNOB_POMTLB && packet->translation_level == 1)
                {
                    pomtlb->insertPOMEntry(cpu_no, packet->pom_address, packet->data, packet->v_address);
                }

                // leaf PTE
                if(packet->translation_level == 1)
                {
                    uint64_t page = packet->v_address >> LOG2_PAGE_SIZE;
                    uint64_t tblock = page >> 3;
                    auto checkPage = tblockmetadata_tracker.find({tblock, cpu_no});
                    if(checkPage == tblockmetadata_tracker.end())
                    {
                        int cache_block_id = tblock & 0x3F; // 6-bit cache block id within page
                        int pte_offset = page & 0x7; // 3-bit offset within cache block
                        tblockmetadata_tracker[{tblock,  cpu_no}] = TblockMetaData(cache_block_id, pte_offset);
                    }
                }

                // // to verify retrieved PTE and cache block it belongs to
                // for(auto entry: pt_cache_block.second)
                // {
                //     dlog.log("Verify @DRAM, addr", intToHex(entry.second.page_address), intToHex(entry.second.virt_page_address), "lookup-result", result.first, intToHex(result.second.page_address), intToHex(result.second.virt_page_address), '\n');
                // }
            }
            else packet->hit_where = CACHE_ID::IS_DRAM;

            // dlog.log(current_cycle, "level", (int)packet->translation_level, intToHex(packet->address), intToHex(packet->v_address), intToHex(packet->data), "pom", packet->pomflag[POM::POM], "pommiss", packet->pomflag[POM::POM_MISS], "pomtoptw", packet->pomflag[POM::POM_TO_PTW], '\n');
            
            // Test: leaf pt request, record VP and PP
            if(packet->translation_level == 1 && packet->type ==TRANSLATION && should_record)
            {
                xlog.log(current_cycle, "DRAM", "addr", intToHex(packet->address), "vaddr", intToHex(packet->v_address), "data", intToHex(packet->data), "cpu", cpu_no, '\n');
            }

            for (auto ret : packet->to_return)
            {
                ret->return_data(packet);
            }

            return -1; // Fast-forward
        }

        // std::cout << "XXXXXXXXXXXXXX\n";
        // std::cout << (packet->address & 0xffffffff)/PAGE_SIZE << ", " << numPPages << '\n';
        // procPageAccess[0] = true;
        // std::cout << "YYYYYYYYYYYYYYY\n";
        // Check for duplicates
        auto rq_it = std::find_if(std::begin(RQ), std::end(RQ), 
                                    eq_addr<PACKET>(packet->address, LOG2_BLOCK_SIZE));
        if (rq_it != std::end(RQ)) 
        { // Duplicate found
            std::cout << "[Meta-RQ] duplicate rq_it->type: " << int(rq_it->type) 
                        << " rq_it->address: " << rq_it->address
                        << " rq_it->cpu: " << rq_it->cpu
                        << " pkt->type: " << int(packet->type) 
                        << " pkt->address: " << packet->address
                        << " pkt->cpu: " << packet->cpu 
                        << " pkt->instr: " << packet->instr_id 
                        << std::endl;

            rq_it->thread_id = packet->thread_id;
            rq_it->scheduled = packet->scheduled;
            rq_it->asid[0] = packet->asid[0], rq_it->asid[1] = packet->asid[1];
            rq_it->type = packet->type;
            rq_it->fill_level = packet->fill_level;
            rq_it->pf_origin_level = packet->pf_origin_level;
            rq_it->pf_metadata = packet->pf_metadata;
            rq_it->cpu = packet->cpu;
            rq_it->address = packet->address;
            rq_it->v_address = packet->v_address;
            rq_it->data = packet->data;
            rq_it->instr_id = packet->instr_id;
            rq_it->ip = packet->ip;
            rq_it->event_cycle = packet->event_cycle;
            rq_it->cycle_enqueued = packet->cycle_enqueued;
            rq_it->to_return.clear();
            rq_it->lq_index_depend_on_me.clear();
            rq_it->sq_index_depend_on_me.clear();
            rq_it->instr_depend_on_me.clear();
            rq_it->translation_level = packet->translation_level;
            rq_it->init_translation_level = packet->init_translation_level;
            rq_it->page_table_base_address = packet->page_table_base_address;
            rq_it->pomflag[POM::POM_TO_PTW] = packet->pomflag[POM::POM_TO_PTW];
            rq_it->pomflag[POM::POM] = packet->pomflag[POM::POM];
            rq_it->pom_address = packet->pom_address;
            
            packet_dep_merge(rq_it->lq_index_depend_on_me, packet->lq_index_depend_on_me);
            packet_dep_merge(rq_it->sq_index_depend_on_me, packet->sq_index_depend_on_me);
            packet_dep_merge(rq_it->instr_depend_on_me, packet->instr_depend_on_me);
            packet_dep_merge(rq_it->to_return, packet->to_return);
            return 0; // Merged
        }
        
        // Find empty slot
        rq_it = std::find_if_not(std::begin(RQ), std::end(RQ), is_valid<PACKET>());
        if (rq_it == std::end(RQ) || memory_system_->WillAcceptTransaction(packet->address, false) == false) {
			std::cout<<"[PANIC] RQ cannot accept entries or DRAMSim3 cannot accept transaction!"<<std::endl;
            assert(0); // This should not happen as we check occupancy before calling add_rq
        }
        // Call to DRAMSim
        memory_system_->AddTransaction(packet->address, false);

        // Add to RQ
        // Remember this packet to later return data
        // *rq_it = *packet;

        rq_it->thread_id = packet->thread_id;
        rq_it->scheduled = packet->scheduled;
        rq_it->asid[0] = packet->asid[0], rq_it->asid[1] = packet->asid[1];
        rq_it->type = packet->type;
        rq_it->fill_level = packet->fill_level;
        rq_it->pf_origin_level = packet->pf_origin_level;
        rq_it->pf_metadata = packet->pf_metadata;
        rq_it->cpu = packet->cpu;
        rq_it->address = packet->address;
        rq_it->v_address = packet->v_address;
        rq_it->data = packet->data;
        rq_it->instr_id = packet->instr_id;
        rq_it->ip = packet->ip;
        rq_it->event_cycle = packet->event_cycle;
        rq_it->cycle_enqueued = packet->cycle_enqueued;
        rq_it->to_return.clear();
        rq_it->lq_index_depend_on_me.clear();
        rq_it->sq_index_depend_on_me.clear();
        rq_it->instr_depend_on_me.clear();
        rq_it->translation_level = packet->translation_level;
        rq_it->init_translation_level = packet->init_translation_level;
        rq_it->page_table_base_address = packet->page_table_base_address;
        rq_it->pomflag[POM::POM_TO_PTW] = packet->pomflag[POM::POM_TO_PTW];
        rq_it->pomflag[POM::POM] = packet->pomflag[POM::POM];
        rq_it->pom_address = packet->pom_address;

        packet_dep_merge(rq_it->lq_index_depend_on_me, packet->lq_index_depend_on_me);
        packet_dep_merge(rq_it->sq_index_depend_on_me, packet->sq_index_depend_on_me);
        packet_dep_merge(rq_it->instr_depend_on_me, packet->instr_depend_on_me);
        packet_dep_merge(rq_it->to_return, packet->to_return);
    
        return std::count_if(std::begin(RQ), std::end(RQ), is_valid<PACKET>());
    }

    int add_wq(PACKET* packet) override {
        if (all_warmup_complete <= NUM_CPUS)
            return -1; // Fast-forward

        // procPageAccess[packet->address/PAGE_SIZE] = true;

        // If DRAMSim cannot take new req, return
        if (!memory_system_->WillAcceptTransaction(packet->address, true)) {
            return -2;
        }
       
        // Call to DRAMSim
        memory_system_->AddTransaction(packet->address, true);
        return 0;
    }

    int add_pq(PACKET* packet) override {
        return add_rq(packet);
    }

    void operate() override {
        memory_system_->ClockTick();
    }
    uint32_t get_occupancy(uint8_t queue_type, uint64_t address) override {
        if (queue_type == 1) {
            if (!memory_system_->WillAcceptTransaction(address, false)) {
                // DRAMSim cannot accept transaction, this addr must not be inserted
                return RQ.size();
            }
            else {
                return std::count_if(std::begin(RQ), std::end(RQ), is_valid<PACKET>());
            }
        }
        else if (queue_type == 2) {
            return memory_system_->WillAcceptTransaction(address, true) ? 0 : RQ.size();
        }
        else if (queue_type == 3)
            return get_occupancy(1, address);

        return -1;        
    }
    uint32_t get_size(uint8_t queue_type, uint64_t address) override {
        if (queue_type == 1)
            return RQ.size();
        else if (queue_type == 2)
            return RQ.size();
        else if (queue_type == 3)
            return get_size(1, address);
        return -1;
    }

    void ReadCallBack(uint64_t addr) { 
        auto rq_pkt = std::find_if(std::begin(RQ), std::end(RQ), 
                                    eq_addr<PACKET>(addr, LOG2_BLOCK_SIZE));

        if (rq_pkt != std::end(RQ)) 
        {
            uint32_t cpu_no = KNOB_SMT_ENABLE*rq_pkt->cpu + rq_pkt->thread_id;
            bool should_record = true;

            if(KNOB_POMTLB && rq_pkt->pomflag[POM::POM]&& !rq_pkt->pomflag[POM::POM_TO_PTW])
            {
                dlog.log(current_cycle, "DRAM-POM-Request, level", (int)rq_pkt->translation_level, intToHex(rq_pkt->address), intToHex(rq_pkt->v_address), intToHex(rq_pkt->data), "pom", rq_pkt->pomflag[POM::POM], "pommiss", rq_pkt->pomflag[POM::POM_MISS], "pomtoptw", rq_pkt->pomflag[POM::POM_TO_PTW], '\n');

                pair<bool, uint64_t> result = pomtlb->lookupPOMEntry(cpu_no, rq_pkt->address, rq_pkt->v_address);
                pair<bool, vector<pair<bool, PTEHolder>>> pomtlb_line = pomtlb->getPOMTLBLine(cpu_no, rq_pkt->address, rq_pkt->v_address);
                
                rq_pkt->page_fault = true;// its a miss rather than page-fault
                rq_pkt->pomflag[POM::POM_MISS] = true;
                if(result.first) // hit in POM-TLB
                {
                    should_record = true;
                    rq_pkt->hit_where = CACHE_ID::IS_DRAM;
                    rq_pkt->data = result.second;
                    rq_pkt->page_fault = false;// its a miss rather than page-fault
                    rq_pkt->pomflag[POM::POM_MISS] = false;

                    rq_pkt->page_table_entries = pomtlb_line.second;
                    dlog.log(current_cycle, "DRAM-POM-HIT, level", (int)rq_pkt->translation_level, intToHex(rq_pkt->address), intToHex(rq_pkt->v_address), intToHex(rq_pkt->data), "pom", rq_pkt->pomflag[POM::POM], "pommiss", rq_pkt->pomflag[POM::POM_MISS], "pomtoptw", rq_pkt->pomflag[POM::POM_TO_PTW], '\n');

                }
            }
            else if(rq_pkt->type == TRANSLATION)
            {
                should_record = true;
                pair<bool, PTEHolder> result = process_page_table->operate_pagetable(cpu_no, rq_pkt->address, rq_pkt->translation_level, rq_pkt->v_address);
                pair<int, vector<pair<bool, PTEHolder>>> pt_cache_block = process_page_table->get_cacheblock_data(cpu_no, rq_pkt->address, rq_pkt->translation_level);

                result.first? 0:pt_page_faulted++;
                rq_pkt->data = result.second.page_address;
                rq_pkt->page_fault = !result.first;
                rq_pkt->hit_where = CACHE_ID::IS_DRAM;
                rq_pkt->page_table_entries = pt_cache_block.second;
                
                if(rq_pkt->pomflag[POM::POM_TO_PTW] && KNOB_POMTLB && rq_pkt->translation_level == 1)
                {
                    pomtlb->insertPOMEntry(cpu_no, rq_pkt->pom_address, rq_pkt->data, rq_pkt->v_address);
                }

                // leaf PTE
                if(rq_pkt->translation_level == 1)
                {
                    uint64_t page = rq_pkt->v_address >> LOG2_PAGE_SIZE;
                    uint64_t tblock = page >> 3;
                    auto checkPage = tblockmetadata_tracker.find({tblock, cpu_no});
                    if(checkPage == tblockmetadata_tracker.end())
                    {
                        int cache_block_id = tblock & 0x3F; // 6-bit cache block id within page
                        int pte_offset = page & 0x7; // 3-bit offset within cache block
                        tblockmetadata_tracker[{tblock,  cpu_no}] = TblockMetaData(cache_block_id, pte_offset);
                    }
                }

                // // To verify result of retrived PTE and the cache-block it belongs to
                // for(auto entry: pt_cache_block.second)
                // {
                //     dlog.log("Verify @DRAM, addr", intToHex(entry.second.page_address), intToHex(entry.second.virt_page_address), "lookup-result", result.first, intToHex(result.second.page_address), intToHex(result.second.virt_page_address), '\n');
                // }
            }
            else rq_pkt->hit_where = CACHE_ID::IS_DRAM;
           
            // dlog.log(current_cycle, "level", (int)rq_pkt->translation_level, intToHex(rq_pkt->address), intToHex(rq_pkt->v_address), intToHex(rq_pkt->data), "pom", rq_pkt->pomflag[POM::POM], "pommiss", rq_pkt->pomflag[POM::POM_MISS], "pomtoptw", rq_pkt->pomflag[POM::POM_TO_PTW], '\n');

            //Test: leaf pt request, record VP and PP
            if(rq_pkt->translation_level == 1 && rq_pkt->type ==TRANSLATION && should_record)
            {
                // record requests
                xlog.log(current_cycle, "DRAM", "addr", intToHex(rq_pkt->address), "vaddr", intToHex(rq_pkt->v_address), "data", intToHex(rq_pkt->data), "cpu", cpu_no, '\n');
            }

            for (auto ret : rq_pkt->to_return) 
            {
                ret->return_data(&(*rq_pkt));
            }
                
            *rq_pkt = {};
        }
        else {
            std::cout << "[PANIC] RQ packet not found on DRAMSim req completion for addr " 
                      << addr << ". Skipping callback to LLC..." << std::endl;
            // assert(0);
        }
    }
    void WriteCallBack(uint64_t addr) { return; }
    void ACTCallBack(uint64_t ch, uint64_t ra, uint64_t ba, uint64_t ro) {
        //DEBUG std::cout << "[ACT] Ch-" << ch << " Ra-" << ra << " Ba-" << ba << " Ro-" << ro << std::endl;
    }
    void PrintStats() { 
        cout<< '\n' << "UserImplemeted Page Table Tracker\n";
        cout << "DRAM Total Allocated DataPages, " << data_page << '\n';
        cout << "DRAM Allocated DataPages (out of Total) Faulted, " << data_page_faulted << '\n';
        cout << "DRAM Total Allocated PageTablePages, " << pt_page << '\n';
        cout << "DRAM Allocated PageTablePages (out of Total) Faulted, " << pt_page_faulted << '\n';
        cout << '\n';
        memory_system_->PrintStats(); }

    void print_deadlock() {
        std::cout << "DRMA RQ\n";
        if(!std::empty(RQ))
        {
            for(auto entry: RQ)
            {
                std::cout << std::hex << entry.address << ", " << entry.v_address << ", " << std::dec << entry.instr_id << '\n'; 
            }
        }
    }
protected:
    dramsim3::MemorySystem* memory_system_;
    std::vector<PACKET> RQ{DRAM_RQ_SIZE*DRAM_CHANNELS}; // Meta-RQ for callbacks
    logger dlog, xlog;
    int data_page, pt_page;
    int data_page_faulted, pt_page_faulted;
};
#endif