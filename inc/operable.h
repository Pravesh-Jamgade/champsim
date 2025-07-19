#ifndef OPERABLE_H
#define OPERABLE_H

#include <iostream>
#include <string>

namespace champsim
{

class operable
{
public:
  const double CLOCK_SCALE;
  std::string NAME="";

  double leap_operation = 0;
  uint64_t current_cycle = 0;

  explicit operable(double scale) : CLOCK_SCALE(scale - 1) {}
  explicit operable(double scale, std::string NAME) : CLOCK_SCALE(scale - 1), NAME(NAME) {}

  void _operate()
  {
    // skip periodically
    if (leap_operation >= 1) {
      leap_operation -= 1;
      return;
    }

    operate();

    leap_operation += CLOCK_SCALE;
    ++current_cycle;
  }

  virtual void operate() = 0;
  virtual void print_deadlock() {}
  virtual void _overwrite() {}
};

class by_next_operate
{
public:
  bool operator()(operable* lhs, operable* rhs) const { return lhs->leap_operation < rhs->leap_operation; }
};

} // namespace champsim

#endif
