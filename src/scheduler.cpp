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

/**
 * This is a per-GPU manager/scheduler.
 * Based on the information provided by clients, it decide which client to run
 * and give token to that client. This scheduler act as a daemon, accepting
 * requests from hook library (frontend).
 */

#include "scheduler.h"

#include <arpa/inet.h>
#include <errno.h>
#include <execinfo.h>
#include <getopt.h>
#include <linux/limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <string>
#include <thread>
#include <typeinfo>
#include <vector>

#include "comm/endpoint.hpp"
#include "debug.h"
#include "parse-config.h"
#include "util.h"

using std::string;
using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::steady_clock;
using std::chrono::time_point;

// signal handler
void sig_handler(int);
#ifdef _DEBUG
void dump_history(int);
#endif

// helper function for getting timespec
struct timespec get_timespec_after(double ms) {
  struct timespec ts;
  // now
  clock_gettime(CLOCK_MONOTONIC, &ts);

  double sec = ms / 1e3;
  ts.tv_sec += floor(sec);
  ts.tv_nsec += (sec - floor(sec)) * 1e9;
  ts.tv_sec += ts.tv_nsec / 1000000000;
  ts.tv_nsec %= 1000000000;
  return ts;
}

// all in milliseconds
double QUOTA = 250.0;
double MIN_QUOTA = 100.0;
double WINDOW_SIZE = 10000.0;
int verbosity = 0;

auto PROGRESS_START = steady_clock::now();

// program options
char limit_file[PATH_MAX] = "resource.conf";
char ipc_dir[PATH_MAX] = "/tmp/gemini/ipc";

void *zeromq_context;  // global zeromq context
// map client name to pointer of ClientGroup
std::map<string, ClientGroup *> client_group_map;

std::list<Candidate> candidates;
pthread_mutex_t candidate_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t candidate_cond;  // initialized with CLOCK_MONOTONIC in main()

std::list<History> history_list;
#ifdef _DEBUG
std::list<History> full_history;
#endif

// milliseconds since scheduler process started
inline double ms_since_start() {
  return duration_cast<microseconds>(steady_clock::now() - PROGRESS_START).count() / 1e3;
}

ClientGroup::ClientGroup(std::string name, double baseq, double minq)
    : kName(name), kBaseQuota(baseq), kMinQuota(minq) {
  quota_ = baseq;
  latest_overuse_ = 0.0;
  latest_actual_usage_ = 0.0;
  burst_ = 0.0;
  sem_init(&token_sem_, 0, 0);  // set initial value to 0
};

ClientGroup::~ClientGroup() { sem_destroy(&token_sem_); };

const string &ClientGroup::getName() { return kName; }

void ClientGroup::updateConstraint(double minf, double maxf, double maxq, size_t mem_limit) {
  min_frac_ = minf;
  max_frac_ = maxf;
  max_quota_ = maxq;
  mem_limit_ = mem_limit;
}

/**
 * Update the end time of client's usage according to the overuse information provided when client
 * sends another token request, and also the timing when token request is received.
 * @param overuse overuse information provided in token request
 */
void ClientGroup::updateReturnTime(double overuse) {
  double now = ms_since_start();
  for (auto it = history_list.rbegin(); it != history_list.rend(); it++) {
    if (it->name == kName) {
      // client may not use up all of the allocated time
      it->end = std::min(now, it->end + overuse);
      latest_actual_usage_ = it->end - it->start;
      break;
    }
  }
  latest_overuse_ = overuse;
#ifdef _DEBUG
  for (auto it = full_history.rbegin(); it != full_history.rend(); it++) {
    if (it->name == kName) {
      it->end = std::min(now, it->end + overuse);
      break;
    }
  }
#endif
}
void ClientGroup::setBurst(double estimated_burst) { burst_ = estimated_burst; }

// add quota allocation information into history
void ClientGroup::record(double quota) {
  History hist;
  hist.name = kName;
  hist.start = ms_since_start();
  hist.end = hist.start + quota;
  history_list.push_back(hist);
#ifdef _DEBUG
  full_history.push_back(hist);
#endif
}

size_t ClientGroup::memLimit() { return mem_limit_; }

double ClientGroup::minFrac() { return min_frac_; }

double ClientGroup::maxFrac() { return max_frac_; }

void ClientGroup::updateQuota() {
  const double UPDATE_RATE = 0.5;  // how drastically will the quota changes

  if (burst_ < 1e-9) {
    // special case when no burst data available (probably no host-synchronous API or kernel burst
    // is too long to be captured in a token period), just fallback to static quota
    quota_ = kBaseQuota;
    DEBUG("%s: fallback to static quota, assign quota: %.3fms", kName.c_str(), quota_);
  } else {
    quota_ = burst_ * UPDATE_RATE + quota_ * (1 - UPDATE_RATE);
    quota_ = std::max(quota_, kMinQuota);   // lowerbound
    quota_ = std::min(quota_, max_quota_);  // upperbound
    DEBUG("%s: burst: %.3fms, assign quota: %.3fms", kName.c_str(), burst_, quota_);
  }
}

double ClientGroup::getQuota() { return quota_; }

// wait until semaphore becomes positive
void ClientGroup::waitToken() { sem_wait(&token_sem_); }

// notify waiting thread
void ClientGroup::giveToken() { sem_post(&token_sem_); }

/**
 * Read resource constraint config file and update client group constraints.
 * If new client group is found, create corresponding ClientGroup and add to client_group_map.
 * @param file_path path to resource config file
 * @return a vector contains pointers to newly created ClientGroup entries.
 */
vector<ClientGroup *> read_resource_config(const char *file_path) {
  vector<ClientGroup *> new_client_groups;

  // Read GPU limit usage
  ConfigFile config_file(file_path);
  for (string group_name : config_file.getGroups()) {
    ClientGroup *group;
    double min_frac = config_file.getDouble(group_name.c_str(), "MinUtil", 0.0);
    double max_frac = config_file.getDouble(group_name.c_str(), "MaxUtil", 1.0);
    size_t mem_limit = config_file.getSize(group_name.c_str(), "MemoryLimit", 1UL << 30);

    // look for existing group information, or create a new one if not found
    if (client_group_map.find(group_name) != client_group_map.end()) {
      group = client_group_map[group_name];
    } else {
      group = new ClientGroup(group_name, QUOTA, MIN_QUOTA);
      client_group_map[group_name] = group;
      new_client_groups.push_back(group);
    }

    // since we reuse existing object, scheduling thread can receive this update
    group->updateConstraint(min_frac, max_frac, max_frac * WINDOW_SIZE, mem_limit);

    INFO("%s MinUtil: %.2f, MaxUtil: %.2f, MemoryLimit: %lu B", group_name.c_str(), min_frac,
         max_frac, mem_limit);
  }
  return new_client_groups;
}

/**
 * Select a candidate whose current usage is less than its limit, and erase it from waiting
 * candidates. If no such candidate exists, calculate the time until time window content changes and
 * sleep until then, or until another candidate comes. If multiple candidates meet the requirement,
 * select one according to scheduling policy.
 * @return selected candidate
 */
Candidate select_candidate() {
  while (true) {
    /* update history list and get usage in a time interval */
    double window_size = WINDOW_SIZE;
    int overlap_cnt = 0, i, j, k;
    std::map<string, double> usage;
    struct container_timestamp {
      string name;
      double time;
    };

    // start/end timestamp of history in this window
    std::vector<container_timestamp> timestamp_vector;
    // instances to check for overlap
    std::vector<string> overlap_check_vector;

    double now = ms_since_start();
    double window_start = now - WINDOW_SIZE;
    double current_time;
    if (window_start < 0) {
      // elapsed time less than a window size
      window_size = now;
    }

    auto beyond_window = [=](const History &h) -> bool { return h.end < window_start; };
    history_list.remove_if(beyond_window);
    for (auto h : history_list) {
      timestamp_vector.push_back({h.name, -h.start});
      timestamp_vector.push_back({h.name, h.end});
      usage[h.name] = 0;
      if (verbosity > 1) {
        printf("{'container': '%s', 'start': %.3f, 'end': %.3f},\n", h.name.c_str(), h.start / 1e3,
               h.end / 1e3);
      }
    }

    /* select the candidate to give token */

    // quick exit if the first one in candidates does not use GPU recently
    auto check_appearance = [=](const History &h) -> bool {
      return h.name == candidates.front().group_ptr->getName();
    };
    if (std::find_if(history_list.begin(), history_list.end(), check_appearance) ==
        history_list.end()) {
      Candidate selected = candidates.front();
      candidates.pop_front();
      return selected;
    }

    // sort by time
    auto ts_comp = [=](container_timestamp a, container_timestamp b) -> bool {
      return std::abs(a.time) < std::abs(b.time);
    };
    std::sort(timestamp_vector.begin(), timestamp_vector.end(), ts_comp);

    /* calculate overall utilization */
    current_time = window_start;
    for (k = 0; k < timestamp_vector.size(); k++) {
      if (std::abs(timestamp_vector[k].time) <= window_start) {
        // start before this time window
        overlap_cnt++;
        overlap_check_vector.push_back(timestamp_vector[k].name);
      } else {
        break;
      }
    }

    // i >= k: events in this time window
    for (i = k; i < timestamp_vector.size(); ++i) {
      // instances in overlap_check_vector overlaps,
      // overlapped interval = current_time ~ i-th timestamp
      for (j = 0; j < overlap_check_vector.size(); ++j)
        usage[overlap_check_vector[j]] +=
            (std::abs(timestamp_vector[i].time) - current_time) / overlap_cnt;

      if (timestamp_vector[i].time < 0) {
        // is a "start" timestamp
        overlap_check_vector.push_back(timestamp_vector[i].name);
        overlap_cnt++;
      } else {
        // is a "end" timestamp
        // remove instance from overlap_check_vector
        for (j = 0; j < overlap_check_vector.size(); ++j) {
          if (overlap_check_vector[j] == timestamp_vector[i].name) {
            overlap_check_vector.erase(overlap_check_vector.begin() + j);
            break;
          }
        }
        overlap_cnt--;
      }

      // advance current_time
      current_time = std::abs(timestamp_vector[i].time);
    }

    /* select the one to execute */
    std::vector<ValidCandidate> vaild_candidates;

    for (auto it = candidates.begin(); it != candidates.end(); it++) {
      string name = it->group_ptr->getName();
      double limit, require, missing, remaining;
      limit = it->group_ptr->maxFrac() * window_size;
      require = it->group_ptr->minFrac() * window_size;
      missing = require - usage[name];
      remaining = limit - usage[name];
      if (remaining > 0)
        vaild_candidates.push_back({missing, remaining, usage[name], it->arrived_time, it});
    }

    if (vaild_candidates.size() == 0) {
      // all candidates reach usage limit
      auto ts = get_timespec_after(history_list.begin()->end - window_start);
      DEBUG("sleep until %ld.%03ld", ts.tv_sec, ts.tv_nsec / 1000000);
      // also wakes up if new requests come in
      pthread_cond_timedwait(&candidate_cond, &candidate_mutex, &ts);
      continue;  // go to begin of loop
    }

    std::sort(vaild_candidates.begin(), vaild_candidates.end(), schd_priority);

    auto selected_iter = vaild_candidates.begin()->iter;
    Candidate result = *selected_iter;
    candidates.erase(selected_iter);
    return result;
  }
}

/**
 * Main function of scheduling thread.
 * In each iteration select one valid (under usage constraint) client group, remove it from waiting
 * queue, and sleep until it returns or given quota expires. In case no valid candidates, it
 * calculates the time when client groups usage changes and sleeps until then (in select_candidate).
 * Scheduling thread also wakes up when new client group comes for token.
 */
void *schedule_daemon_func(void *) {
  double quota;

  while (1) {
    pthread_mutex_lock(&candidate_mutex);
    if (candidates.size() != 0) {
      string selected_name;
      // remove an entry from candidates
      Candidate selected = select_candidate();
      pthread_mutex_unlock(&candidate_mutex);

      selected_name = selected.group_ptr->getName();

      DEBUG("select %s, waiting time: %.3f ms", selected_name.c_str(),
            ms_since_start() - selected.arrived_time);

      selected.group_ptr->updateQuota();
      quota = selected.group_ptr->getQuota();
      selected.group_ptr->record(quota);

      // wake the waiting thread up
      // group management thread will send token to client group
      selected.group_ptr->giveToken();

      struct timespec ts = get_timespec_after(quota);

      // wait until the selected one's quota time out
      bool should_wait = true;
      pthread_mutex_lock(&candidate_mutex);
      while (should_wait) {
        int rc = pthread_cond_timedwait(&candidate_cond, &candidate_mutex, &ts);
        if (rc == ETIMEDOUT) {
          DEBUG("%s didn't return on time", selected_name.c_str());
          should_wait = false;
        } else {
          // check if the selected one comes back
          for (auto conn : candidates) {
            if (conn.group_ptr->getName() == selected_name) {
              should_wait = false;
              break;
            }
          }
        }
      }
      pthread_mutex_unlock(&candidate_mutex);
    } else {
      // wait for new client group comes
      DEBUG("no candidates");
      pthread_cond_wait(&candidate_cond, &candidate_mutex);
      pthread_mutex_unlock(&candidate_mutex);
    }
  }
}

// store allocated memory size and last heartbeat record
using MemoryUsageRecord = pair<size_t, time_point<steady_clock>>;

/**
 * Remove peers which are considered dead (heartbeat timeout).
 * @param peers_status a std::map contains MemoryUsageRecord of peers
 * @return reclaimed bytes
 */
size_t removeDeadPeers(std::map<string, MemoryUsageRecord> &peers_status) {
  const int64_t kHeartbeatTimeoutSec = 10;
  size_t reclaimed_bytes = 0;

  time_point<steady_clock> current_time = steady_clock::now();
  vector<string> dead_peers;
  for (auto peer : peers_status) {
    if (duration_cast<std::chrono::seconds>(current_time - peer.second.second).count() >
        kHeartbeatTimeoutSec) {
      dead_peers.push_back(peer.first);
    }
  }
  for (auto peer : dead_peers) {
    WARNING("Client %s is considered dead, reclaiming %lu bytes", peer.c_str(),
            peers_status[peer].first);
    reclaimed_bytes += peers_status[peer].first;
    peers_status.erase(peer);
  }
  return reclaimed_bytes;
}

/**
 * Main function of a client group management thread.
 * A client group management thread receives request from clients via zeromq socket, and respond to
 * client with client group statistics. When needed (e.g. token expired), it also forward the
 * request to main scheduler thread and wait for response in a "blocking" state.
 * @param args pointer to a ClientGroup instance
 */
void *clientGroupMgmt(void *args) {
  ClientGroup *group = static_cast<ClientGroup *>(args);
  char url[PATH_MAX];
  snprintf(url, PATH_MAX, "ipc://%s/%s", ipc_dir, group->getName().c_str());
  Responder responder(zeromq_context, url);

  // allocated memory size and last heartbeat record of each client (peer)
  size_t total_used_memory = 0UL;
  std::map<string, MemoryUsageRecord> peers_status;
  time_point<steady_clock> token_expire = steady_clock::now();

  while (true) {
    Request req;
    Response *rsp = nullptr;
    responder.getRequest(&req);

    // add fields in peers_status for requests from new client (peer)
    if (peers_status.find(req.from()) == peers_status.end()) {
      peers_status[req.from()] = {0, time_point<steady_clock>::min()};
    }
    // update heartbeat record
    peers_status[req.from()].second = steady_clock::now();

    if (req.what() == kHeartbeat) {
      // nothing to do, just reply
      rsp = new HeartbeatResponse();
    } else if (req.what() == kMemInfo) {
      // refresh memory usage
      total_used_memory -= removeDeadPeers(peers_status);
      rsp = new MemInfoResponse(total_used_memory, group->memLimit());
    } else if (req.what() == kMemAlloc) {
      // refresh memory usage
      total_used_memory -= removeDeadPeers(peers_status);

      MemAllocRequest mem_alloc_req(req);
      // check memory limit
      if (mem_alloc_req.isIncrease()) {
        if (total_used_memory + mem_alloc_req.deltaSize() <= group->memLimit()) {
          total_used_memory += mem_alloc_req.deltaSize();
          peers_status[req.from()].first += mem_alloc_req.deltaSize();
          rsp = new MemAllocResponse(true);
        } else {
          rsp = new MemAllocResponse(false);
        }
      } else {
        total_used_memory -= std::min(mem_alloc_req.deltaSize(), total_used_memory);
        peers_status[req.from()].first -=
            std::min(mem_alloc_req.deltaSize(), peers_status[req.from()].first);
        rsp = new MemAllocResponse(true);
      }
    } else if (req.what() == kToken) {
      TokenRequest token_req(req);
      double remain = duration_cast<milliseconds>(token_expire - steady_clock::now()).count();
      if (remain < token_req.nextBurst()) {
        // remaining time is not enough
        group->updateReturnTime(token_req.overuse());
        group->setBurst(token_req.nextBurst());

        // push group into waiting queue
        pthread_mutex_lock(&candidate_mutex);
        candidates.push_back({group, ms_since_start()});
        pthread_cond_signal(&candidate_cond);
        pthread_mutex_unlock(&candidate_mutex);

        // block and wait for response
        // if waitToken returns immediately, it means that scheduling daemon already gives out token
        group->waitToken();

        remain = group->getQuota();
        token_expire = steady_clock::now() + milliseconds(static_cast<int64_t>(remain));
      }
      rsp = new TokenResponse(remain);
    } else {
      ERROR("Client group %s received an unknown request from client %s", group->getName().c_str(),
            req.from().c_str());
      rsp = new Response();
    }

    responder.sendResponse(*rsp);
    delete rsp;
    rsp = nullptr;
  }

  pthread_exit(nullptr);
}

// Create client group management threads in detached state.
void spawnClientGroupThreads(const vector<ClientGroup *> groups) {
  for (ClientGroup *group : groups) {
    pthread_t tid;
    int rc = pthread_create(&tid, nullptr, clientGroupMgmt, group);
    if (rc != 0) {
      ERROR("Failed to create client group management thread: %s", strerror(rc));
      exit(rc);
    }
    pthread_detach(tid);
  }
}

// Monitor the change to resource config file, and spawn new client group management threads if
// needed.
void monitorResourceConfigFile(const char *file_path) {
  const size_t kEventSize = sizeof(struct inotify_event);
  const size_t kBufLen = 1024 * (kEventSize + 16);
  int fd, wd;

  char *dir = g_path_get_dirname(file_path);
  char *base_name = g_path_get_basename(file_path);

  // Initialize Inotify
  fd = inotify_init();
  if (fd < 0) {
    ERROR("Failed to initialize inotify");
    return;
  }

  // add watch to starting directory
  wd = inotify_add_watch(fd, dir, IN_CLOSE_WRITE);

  if (wd == -1) {
    ERROR("Failed to add watch to '%s'.", dir);
    return;
  } else {
    INFO("Watching '%s'.", dir);
  }

  // start watching
  while (true) {
    int i = 0;
    char buffer[kBufLen];
    int length = read(fd, buffer, kBufLen);
    if (length < 0) ERROR("Read error");

    while (i < length) {
      struct inotify_event *event = (struct inotify_event *)&buffer[i];

      if (event->len) {
        if (event->mask & IN_CLOSE_WRITE) {
          // if event is triggered by target file
          if (strcmp((const char *)event->name, base_name) == 0) {
            INFO("Update resource configurations...");
            vector<ClientGroup *> new_client_groups = read_resource_config(file_path);
            spawnClientGroupThreads(new_client_groups);
          }
        }
      }
      // update index to start of next event
      i += kEventSize + event->len;
    }
  }

  // Clean up
  // Supposed to be unreached.
  inotify_rm_watch(fd, wd);
  close(fd);
}

int main(int argc, char *argv[]) {
  // parse command line options
  const char *optstring = "p:q:m:w:f:v:h";
  struct option opts[] = {{"ipc_dir", required_argument, nullptr, 'p'},
                          {"quota", required_argument, nullptr, 'q'},
                          {"min_quota", required_argument, nullptr, 'm'},
                          {"window", required_argument, nullptr, 'w'},
                          {"limit_file", required_argument, nullptr, 'f'},
                          {"verbose", required_argument, nullptr, 'v'},
                          {"help", no_argument, nullptr, 'h'},
                          {nullptr, 0, nullptr, 0}};
  int opt;
  while ((opt = getopt_long(argc, argv, optstring, opts, NULL)) != -1) {
    switch (opt) {
      case 'p':
        strncpy(ipc_dir, optarg, PATH_MAX);
        break;
      case 'q':
        QUOTA = atof(optarg);
        break;
      case 'm':
        MIN_QUOTA = atof(optarg);
        break;
      case 'w':
        WINDOW_SIZE = atof(optarg);
        break;
      case 'f':
        strncpy(limit_file, optarg, PATH_MAX);
        break;
      case 'v':
        verbosity = atoi(optarg);
        break;
      case 'h':
        printf("usage: %s [options]\n", argv[0]);
        puts("Options:");
        puts("    -p [IPC_DIR], --ipc_dir [IPC_DIR]");
        puts("    -q [QUOTA], --quota [QUOTA]");
        puts("    -m [MIN_QUOTA], --min_quota [MIN_QUOTA]");
        puts("    -w [WINDOW_SIZE], --window [WINDOW_SIZE]");
        puts("    -f [LIMIT_FILE], --limit_file [LIMIT_FILE]");
        puts("    -v [LEVEL], --verbose [LEVEL]");
        puts("    -h, --help");
        return 0;
      default:
        break;
    }
  }

  if (verbosity > 0) {
    printf("Scheduler settings:\n");
    printf("    %-20s %.3f ms\n", "default quota:", QUOTA);
    printf("    %-20s %.3f ms\n", "minimum quota:", MIN_QUOTA);
    printf("    %-20s %.3f ms\n", "time window:", WINDOW_SIZE);
  }

  // register signal handler for debugging
  signal(SIGSEGV, sig_handler);
#ifdef _DEBUG
  if (verbosity > 0) signal(SIGINT, dump_history);
#endif

  zeromq_context = zmq_ctx_new();

  // read configuration file
  vector<ClientGroup *> groups = read_resource_config(limit_file);

  // create directory for IPC files
  if (g_mkdir_with_parents(ipc_dir, 0777) == 0) {
    INFO("Create %s for IPC files", ipc_dir);
  } else {
    ERROR("Failed to create directory (%s) for IPC files: %s", ipc_dir, strerror(errno));
    zmq_ctx_term(zeromq_context);  // clean up
    exit(EXIT_FAILURE);
  }

  // initialize candidate_cond with CLOCK_MONOTONIC
  pthread_condattr_t attr_monotonic_clock;
  pthread_condattr_init(&attr_monotonic_clock);
  pthread_condattr_setclock(&attr_monotonic_clock, CLOCK_MONOTONIC);
  pthread_cond_init(&candidate_cond, &attr_monotonic_clock);

  pthread_t tid;
  int rc = pthread_create(&tid, NULL, schedule_daemon_func, NULL);
  if (rc != 0) {
    ERROR("Return code from pthread_create(): %d", rc);
    exit(rc);
  }
  pthread_detach(tid);

  spawnClientGroupThreads(groups);

  // Main thread complete creating initial child threads.
  // The remaining job is to watch for newcomers (new ClientGroup).
  monitorResourceConfigFile(limit_file);

  zmq_ctx_term(zeromq_context);
  return 0;
}

void sig_handler(int sig) {
  void *arr[10];
  size_t s;
  s = backtrace(arr, 10);
  ERROR("Received signal %d", sig);
  backtrace_symbols_fd(arr, s, STDERR_FILENO);
  exit(sig);
}

#ifdef _DEBUG
// write full history to a json file
void dump_history(int sig) {
  char filename[20];
  sprintf(filename, "%ld.json", time(NULL));
  FILE *f = fopen(filename, "w");
  fputs("[\n", f);
  for (auto it = full_history.begin(); it != full_history.end(); it++) {
    fprintf(f, "\t{\"container\": \"%s\", \"start\": %.3lf, \"end\" : %.3lf}", it->name.c_str(),
            it->start / 1000.0, it->end / 1000.0);
    if (std::next(it) == full_history.end())
      fprintf(f, "\n");
    else
      fprintf(f, ",\n");
  }
  fputs("]\n", f);
  fclose(f);

  INFO("history dumped to %s", filename);
  exit(0);
}
#endif
