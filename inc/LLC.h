#ifndef LLC_H
#define LLC_H

#include "memory_class.h"
#include "cache.h"
#include <iostream>
#include <vector>

class LLC:public MEMORY
{
  public:
  vector<CACHE> banks;

  LLC();
};

#endif