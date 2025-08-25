#ifndef PAGE_TABLE_H
#define PAGE_TABLE_H
#include <bits/stdc++.h>
using namespace std;

class BlockOffset
{
    public:
    map<uint64_t, uint64_t> PTE;
};

class CacheBlock
{
    public:
    // blockoffset-number
    map<uint64_t, BlockOffset> blockOffset;
    
};

class Page
{
    public:
    // cacheblock-number
    map<uint64_t, CacheBlock> cacheBlock;
};

class PageTable
{
    public:
    // page-number 
    map<uint64_t, Page> Pages;
};

#endif