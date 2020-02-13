#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <list>
#include <map>
#include <string>

#include "comm.h"

struct History {
  std::string name;
  double start;
  double end;
};

// some bias used for self-adaptive quota calculation
const double EXTRA_QUOTA = 10.0;
const double SCHD_OVERHEAD = 2.0;

class ClientInfo {
 public:
  ClientInfo(double baseq, double minq, double maxq, double minf, double maxf);
  ~ClientInfo();
  void set_burst(double burst);
  void set_window(double window);
  void update_return_time(double overuse);
  void Record(double quota);
  double get_min_fraction();
  double get_max_fraction();
  double get_quota();
  std::map<unsigned long long, size_t> memory_map;
  std::string name;
  size_t gpu_mem_limit;

 private:
  const double MIN_FRAC;    // min percentage of GPU compute resource usage
  const double MAX_FRAC;    // max percentage of GPU compute resource usage
  const double BASE_QUOTA;  // from command line argument
  const double MIN_QUOTA;   // from command line argument
  const double MAX_QUOTA;   // calculated from time window and max fraction
  double quota_;
  double latest_actual_usage_;  // client may return eariler (before quota expire)
  double latest_burst_;         // duration of kernel burst
  double latest_window_;        // duration of window period
};

// the connection to specific container
struct candidate_t {
  int socket;
  std::string name;  // container name
  reqid_t req_id;
  double arrived_time;
};

struct valid_candidate_t {
  double missing;    // requirement - usage
  double remaining;  // limit - usage
  double usage;
  double arrived_time;
  std::list<candidate_t>::iterator iter;
};

bool schd_priority(const valid_candidate_t &a, const valid_candidate_t &b);

#endif
