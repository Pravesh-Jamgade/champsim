#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

#define REGION_SIZE   (1ULL << 44)  // 16 TB virtual
#define PGD_STEP      (1ULL << 39)  // 512 GB
#define PUD_STEP      (1ULL << 30)  // 1 GB
#define PMD_STEP      (1ULL << 21)  // 2 MB
#define PTE_STEP      (1ULL << 12)  // 4 KB

// keep resident memory bounded per window (tweak to your RAM)
#define WINDOW_BYTES  (32ULL << 30) // 32 GB

int main(void) {
    uint8_t *region = (uint8_t*)mmap(NULL, REGION_SIZE, PROT_READ|PROT_WRITE,
                           MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
    if (region == MAP_FAILED) { perror("mmap"); return 1; }

    // OPTIONAL: disable THP manually before running to avoid PMD-huge mappings:
    //   echo never | sudo tee /sys/kernel/mm/transparent_hugepage/enabled

    for (uint64_t pgd_off = 0; pgd_off < REGION_SIZE; pgd_off += PGD_STEP) {

        // Process this PGD-sized chunk in windows to cap RSS
        for (uint64_t win_base = pgd_off; win_base < pgd_off + PGD_STEP; ) {
            uint64_t win = WINDOW_BYTES;
            if (win_base + win > pgd_off + PGD_STEP) win = pgd_off + PGD_STEP - win_base;

            // Iterate PUD entries (1 GB) within this window
            for (uint64_t pud_off = win_base; pud_off < win_base + win; pud_off += PUD_STEP) {

                // Iterate PMD entries (2 MB) in this PUD
                for (uint64_t pmd_off = pud_off; pmd_off < pud_off + PUD_STEP; pmd_off += PMD_STEP) {
                    // Touch ONE 4KB in this PMD to:
                    //  - allocate PUD (first time in this 1GB),
                    //  - allocate PMD (first time in this 2MB),
                    //  - allocate PTE page (L1),
                    //  - allocate a single data page (4KB)
                    volatile uint8_t *addr = region + pmd_off; // k = 0 → first PTE in the PMD
                    *addr = 1;
                }
            }

            // Reclaim the window to keep RSS bounded
            if (madvise(region + win_base, win, MADV_DONTNEED) != 0) perror("madvise");

            win_base += win;
        }
    }

    munmap(region, REGION_SIZE);
    return 0;
}
