#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>

#define NUM_FAULTS (1 << 20)            // 2^20 faults
#define REGION_SIZE ((size_t)1 << 44)   // 16 TB (enough to span PUDs and PMDs)
#define PAGE_SIZE 4096UL
#define PTE_SIZE  (1UL << 12)           // 4KB
#define PMD_SIZE  (1UL << 21)           // 2MB
#define PUD_SIZE  (1UL << 30)           // 1GB

int main() {
    // Map huge virtual region (not backed by physical memory yet)
    uint8_t *region = (uint8_t*)mmap(NULL, REGION_SIZE, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

    if (region == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    // // Trigger faults spaced 2MB apart (PMD level)
    // for (uint64_t i = 0; i < NUM_FAULTS; i++) {
    //     volatile uint8_t *addr = region + i * PMD_SIZE;
    //     *addr = 1; // Trigger page fault
    // }


    // Trigger faults spaced 1GB apart 
    for (uint64_t i = 0; i < (1<<0); i++) {

         // Trigger faults spaced 2MB apart
        for (uint64_t j = 0; j < (1<<10); j++) {
            
            uint8_t *base  = region +  PUD_SIZE * i + PMD_SIZE * j;
            
             // Trigger faults spaced 4KB apart
            for (uint64_t k = 0; k < (1<<20); k++) {
                // Trigger faults spaced 4KB apart 
                volatile uint8_t *addr = region +  *base + PTE_SIZE * k;
                *addr = 1; // Trigger page fault
            }

            // uint8_t *end_addr  = region +  PUD_SIZE * i + PMD_SIZE * j + PTE_SIZE * ((1<<20)-1);
            // if(madvise(base, end_addr-base, MADV_FREE)!=0)
            // {
            //     perror("madvise");
            // }
        }
    }

   

    printf("Done triggering %d PMD faults\n", NUM_FAULTS);

    munmap(region, REGION_SIZE);
    return 0;
}
