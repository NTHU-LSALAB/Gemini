#include "scheduler.h"

bool schd_priority(const valid_candidate_t &a, const valid_candidate_t &b) {
  if (a.missing > 0 && b.missing > 0)
    return a.missing / (a.missing + a.usage) > b.missing / (b.missing + b.usage);
  if (a.missing > 0 && b.missing < 0) return true;
  if (a.missing < 0 && b.missing > 0) return false;
  // return a.arrived_time < b.arrived_time; // first-arrival first
  return a.usage < b.usage;  // minimum usage first
}
