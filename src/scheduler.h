/**
 * Copyright 2020 Hung-Hsin Chen, LSA Lab, National Tsing Hua University
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <semaphore.h>

#include <list>
#include <map>
#include <string>

struct History {
  std::string name;
  double start;
  double end;
};

// some bias used for self-adaptive quota calculation
const double EXTRA_QUOTA = 10.0;
const double SCHD_OVERHEAD = 2.0;

class ClientGroup {
 public:
  ClientGroup(std::string name, double baseq, double minq);
  ~ClientGroup();
  // allows scheduler to know client group status and perform certain operations
  enum Status { kInactive, kRunning, kWaitingPrefetchToken, kPrefetching };
  Status getStatus();
  void setStatus(Status status);

  const std::string &getName();
  void updateConstraint(double minf, double maxf, double maxq, size_t mem_limit);
  void updateReturnTime(double overuse);
  void setBurst(double burst);
  void record(double quota);
  size_t memLimit();
  double minFrac();
  double maxFrac();
  double getQuota();
  void updateQuota();
  void waitKernelToken(size_t memory_usage);
  void waitPrefetchToken();
  void giveKernelToken(size_t peer_memory_usage = 0);
  void givePrefetchToken(size_t peer_memory_usage);
  size_t getExpectedMemoryUsage();
  size_t getPeerMemoryUsage();
  std::map<unsigned long long, size_t> memory_map;

 private:
  const std::string kName;
  const double kBaseQuota;  // from command line argument
  const double kMinQuota;   // from command line argument
  double mem_limit_;
  double min_frac_;   // min percentage of GPU compute resource usage
  double max_frac_;   // max percentage of GPU compute resource usage
  double max_quota_;  // calculated from time window and max fraction
  double quota_;
  double latest_overuse_;
  double latest_actual_usage_;  // client may return eariler (before quota expire)
  double burst_;                // duration of kernel burst
  sem_t kernel_token_sem_, prefetch_token_sem_;
  size_t expected_memory_usage_;
  size_t peer_memory_usage_;  // max allowed prefetch size = total GPU memory - peer memory usage
  Status status_;
};

struct Candidate {
  ClientGroup *group_ptr;
  double arrived_time;
};

struct ValidCandidate {
  double missing;    // requirement - usage
  double remaining;  // limit - usage
  double usage;
  double arrived_time;
  std::list<Candidate>::iterator iter;
};

bool schd_priority(const ValidCandidate &a, const ValidCandidate &b);

#endif
