#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#define REGION_SIZE   (1ULL << 44)   // 16 TB virtual
#define STRIDE        (1ULL << 12)   // choose: 4KB (PTE) / 2MB (PMD) / 1GB (PUD)
#define WINDOW_BYTES  (64ULL << 30)  // 64 GB window to cap RSS

int main(void) {
    uint8_t *region = (uint8_t*)mmap(NULL, REGION_SIZE, PROT_READ|PROT_WRITE,
                           MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
    if (region == MAP_FAILED) { perror("mmap"); return 1; }

    for (unsigned long long base = 0; base < REGION_SIZE; base += WINDOW_BYTES) {
        unsigned long long window = WINDOW_BYTES;
        if (base + window > REGION_SIZE) window = REGION_SIZE - base;

        // Touch this window
        unsigned long long steps = window / STRIDE;
        for (unsigned long long i = 0; i < steps; i++) {
            volatile uint8_t *addr = region + base + i*STRIDE;
            *addr = 1;
        }

        // Drop the window to keep RSS bounded
        if (madvise(region + base, (window + 4095) & ~4095ULL, MADV_DONTNEED) != 0)
            perror("madvise");
    }

    munmap(region, REGION_SIZE);
    return 0;
}
