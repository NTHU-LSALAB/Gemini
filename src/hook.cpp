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
 * The library to intercept applications' CUDA-related function calls.
 *
 * This hook library will try connecting to scheduling system when first intercepted function being
 * called (with information specified in environment variables). After then, all CUDA kernel
 * launches and some GPU memory-related activity will be controlled by this hook library.
 */

#define __USE_GNU
#include "hook.h"

#include <arpa/inet.h>
#include <cuda.h>
#include <cuda_runtime.h>
#include <dlfcn.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "comm.h"
#include "debug.h"
#include "predictor.h"
#include "util.h"

extern "C" {
void *__libc_dlsym(void *map, const char *name);
}
extern "C" {
void *__libc_dlopen_mode(const char *name, int mode);
}

#define STRINGIFY(x) #x
#define CUDA_SYMBOL_STRING(x) STRINGIFY(x)
#define SYNCP_MESSAGE

typedef void *(*fnDlsym)(void *, const char *);

static void *real_dlsym(void *handle, const char *symbol) {
  static fnDlsym internal_dlsym =
      (fnDlsym)__libc_dlsym(__libc_dlopen_mode("libdl.so.2", RTLD_LAZY), "dlsym");
  return (*internal_dlsym)(handle, symbol);
}

struct hookInfo {
  int debug_mode;
  void *preHooks[NUM_HOOK_SYMBOLS];
  void *postHooks[NUM_HOOK_SYMBOLS];
  int call_count[NUM_HOOK_SYMBOLS];

  hookInfo() {
    const char *envHookDebug;

    envHookDebug = getenv("CU_HOOK_DEBUG");
    if (envHookDebug && envHookDebug[0] == '1')
      debug_mode = 1;
    else
      debug_mode = 0;
  }
};

static struct hookInfo hook_inf;

/*
 ** interposed functions
 */
void *dlsym(void *handle, const char *symbol) {
  // Early out if not a CUDA driver symbol
  if (strncmp(symbol, "cu", 2) != 0) {
    return (real_dlsym(handle, symbol));
  }

  if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemAlloc)) == 0) {
    return (void *)(&cuMemAlloc);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemAllocManaged)) == 0) {
    return (void *)(&cuMemAllocManaged);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemAllocPitch)) == 0) {
    return (void *)(&cuMemAllocPitch);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemFree)) == 0) {
    return (void *)(&cuMemFree);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuArrayCreate)) == 0) {
    return (void *)(&cuArrayCreate);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuArray3DCreate)) == 0) {
    return (void *)(&cuArray3DCreate);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuArrayDestroy)) == 0) {
    return (void *)(&cuArrayDestroy);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMipmappedArrayCreate)) == 0) {
    return (void *)(&cuMipmappedArrayCreate);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMipmappedArrayDestroy)) == 0) {
    return (void *)(&cuMipmappedArrayDestroy);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuLaunchKernel)) == 0) {
    return (void *)(&cuLaunchKernel);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuLaunchCooperativeKernel)) == 0) {
    return (void *)(&cuLaunchCooperativeKernel);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemGetInfo)) == 0) {
    return (void *)(&cuMemGetInfo);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuCtxSynchronize)) == 0) {
    return (void *)(&cuCtxSynchronize);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemcpyAtoH)) == 0) {
    return (void *)(&cuMemcpyAtoH);
    // } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemcpyAtoHAsync)) == 0) {
    //   return (void *)(&cuMemcpyAtoHAsync);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemcpyDtoH)) == 0) {
    return (void *)(&cuMemcpyDtoH);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemcpyDtoHAsync)) == 0) {
    return (void *)(&cuMemcpyDtoHAsync);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemcpyHtoA)) == 0) {
    return (void *)(&cuMemcpyHtoA);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemcpyHtoD)) == 0) {
    return (void *)(&cuMemcpyHtoD);
  // } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuCtxGetCurrent)) == 0) {
  //   return (void *)(&cuCtxGetCurrent);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuCtxSetCurrent)) == 0) {
    return (void *)(&cuCtxSetCurrent);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuCtxDestroy)) == 0) {
    return (void *)(&cuCtxDestroy);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuCtxCreate)) == 0) {
    return (void *)(&cuCtxCreate);
  // } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuDevicePrimaryCtxRetain)) == 0) {
  //   return (void *)(&cuDevicePrimaryCtxRetain);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuDevicePrimaryCtxReset)) == 0) {
    return (void *)(&cuDevicePrimaryCtxReset);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuDevicePrimaryCtxRelease)) == 0) {
    return (void *)(&cuDevicePrimaryCtxRelease);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuCtxPopCurrent)) == 0) {
    return (void *)(&cuCtxPopCurrent);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuCtxPushCurrent)) == 0) {
    return (void *)(&cuCtxPushCurrent);
  }
  // omit cuDeviceTotalMem here so there won't be a deadlock in cudaEventCreate when we are in
  // initialize(). Functions called by cliet are still being intercepted.
  return (real_dlsym(handle, symbol));
}

/* Support max gpu number and current number */
const int max_gpu_num = 8;
int current_gpu_num = 0;

/* connection with Pod manager */
char pod_manager_ip[20] = "127.0.0.1";
uint16_t pod_manager_port = 50052;  // default value
std::vector<uint16_t> pod_manager_port_list;

pthread_mutex_t comm_mutex = PTHREAD_MUTEX_INITIALIZER;  // one communication at a time
const int NET_OP_MAX_ATTEMPT = 5;  // maximum time retrying failed network operations
const int NET_OP_RETRY_INTV = 10;  // seconds between two retries

/* GPU computation resource usage */
double quota_time[max_gpu_num] = {0};  // time quota from scheduler
double overuse[max_gpu_num] = {0};     // overuse time (ms)
pthread_mutex_t request_time_mutex = PTHREAD_MUTEX_INITIALIZER;

// predictors
const double SCHD_OVERHEAD = 2.0;  // ms
Predictor burst_predictor[2] = {Predictor("burst", SCHD_OVERHEAD), Predictor("burst", SCHD_OVERHEAD)};
Predictor window_predictor[2] = {Predictor("window"), Predictor("window")};
// Predictor burst_predictor("burst", SCHD_OVERHEAD);  // predicted burst may not be the full burst
// Predictor window_predictor("window");

pthread_mutex_t expiration_status_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_once_t init_done = PTHREAD_ONCE_INIT;
cudaEvent_t cuevent_start[max_gpu_num];      // the time receive new token
struct timespec request_start;  // the time receive new token

pthread_mutex_t overuse_trk_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t overuse_trk_strt_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t overuse_trk_cmpl_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t overuse_trk_intr_cond;  // initialize it with CLOCK_MONOTONIC
bool overuse_trk_cmpl;

// GPU memory allocation information
pthread_mutex_t allocation_mutex = PTHREAD_MUTEX_INITIALIZER;
std::map<CUdeviceptr, size_t> allocation_map[max_gpu_num];
size_t gpu_mem_used[max_gpu_num] = {0};  // local accounting only

static int sockfd[max_gpu_num];

/**
 * get elapsed us since certain time
 * @param begin starting time
 * @return microseconds since starting time
 */
long long us_since(struct timespec begin) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (now.tv_sec - begin.tv_sec) * 1000000LL + (now.tv_nsec - begin.tv_nsec) / 1000LL;
}

/**
 * Handle get current device id
 */
int get_current_device_id() {
  CUdevice device;
  CUresult rc = cuCtxGetDevice(&device);
  if (rc != CUDA_SUCCESS || device >= max_gpu_num) {
    ERROR("failed to get current device: %d", rc);
  }

  // DEBUG("Operation on device %d", device);
  return device;
}

/**
 * get connection information from environment variables
 */
void configure_connection() {
  // get Pod manager IP, default 127.0.0.1
  char *ip = getenv("POD_MANAGER_IP");
  if (ip != NULL) strcpy(pod_manager_ip, ip);

  // get Pod manager port, default 50052
  char *port = getenv("POD_MANAGER_PORT");
  if (port == NULL) {
    ERROR("Fail to read POD_MANAGER_PORTS.");
  } else {
    // Handle string split
    const char s[2] = ",";
    char *token;

    token = strtok(port, s);
    while (token != NULL) {
      pod_manager_port_list.push_back(atoi(token));
      token = strtok(NULL, s);
    }

    for (auto i : pod_manager_port_list) {
      DEBUG("Pod manager port: %d", i);
    }

    current_gpu_num = pod_manager_port_list.size();
  }
}

/**
 * establish connection with scheduler.
 * @return connected socket file descriptor
 */
int establish_connection(int device) {
  int sockfd_e = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd_e == -1) {
    ERROR("Failed to create socket.");
    exit(-1);
  }


  // DEBUG("Config connection to : %d", device);
  struct sockaddr_in info;
  bzero(&info, sizeof(info));
  info.sin_family = PF_INET;
  info.sin_addr.s_addr = inet_addr(pod_manager_ip);
  info.sin_port = htons(pod_manager_port_list[device]);

  //DEBUG("Trying to establish connection on port: %d", pod_manager_port_list[device]);

  int rc = multiple_attempt(
      [&]() -> int { return connect(sockfd_e, (struct sockaddr *)&info, sizeof(info)); },
      NET_OP_MAX_ATTEMPT, NET_OP_RETRY_INTV);
  if (rc != 0) {
    ERROR("Connection error: %s", strerror(rc));
    exit(rc);
  }

  return sockfd_e;
}

/**
 * Unified communication method with Pod manager/scheduler.
 * Send a request and receive a response.
 * Only one thread can communicate at the same time.
 * @param sbuf buffer with the data to send.
 * @param rbuf buffer which will be filled with received data.
 * @param socket_timeout socket timeout (second), 0 means never timeout
 * @return buffer with received data
 */
int communicate(char *sbuf, char *rbuf, int socket_timeout, int device) {
  // static int sockfd[max_gpu_num];

  sockfd[device] = establish_connection(device);
  //DEBUG("establish_connection to device: %d", device);
  int rc;
  struct timeval tv;
  // CUdevice device;
  // cuCtxGetDevice(&device);
  // std::cout << device << std::endl;

  pthread_mutex_lock(&comm_mutex);

  // set socket timeout
  tv = {socket_timeout, 0};
  setsockopt(sockfd[device], SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  // perform communication
  rc = multiple_attempt(
      [&]() -> int {
        if (send(sockfd[device], sbuf, REQ_MSG_LEN, 0) == -1) return -1;
        if (recv(sockfd[device], rbuf, RSP_MSG_LEN, 0) == -1) return -1;
        return 0;
      },
      NET_OP_MAX_ATTEMPT);

  pthread_mutex_unlock(&comm_mutex);
  //DEBUG("Communicate to device : %d", device);
  return rc;
}

/**
 * Record a host-side synchronous call and update predictor statistics.
 * @param func_name name of synchronous call
 */
void host_sync_call(const char *func_name) {
#ifdef SYNCP_MESSAGE
  DEBUG("SYNC (%s)", func_name);
#endif
  int device = get_current_device_id();
  burst_predictor[device].record_stop();
  window_predictor[device].record_start();
}

/**
 * get available GPU memory from Pod manager/scheduler
 * assume user memory limit won't exceed hardware limit
 * @return remaining memory, memory limit
 */
std::pair<size_t, size_t> get_gpu_memory_info() {
  char sbuf[REQ_MSG_LEN], rbuf[RSP_MSG_LEN], *attached;
  size_t rpos = 0;
  int rc;
  size_t used, total;

  bzero(sbuf, REQ_MSG_LEN);
  prepare_request(sbuf, REQ_MEM_LIMIT);

  int device = get_current_device_id();
  // get data from Pod manager
  rc = communicate(sbuf, rbuf, NET_OP_RETRY_INTV, device);
  if (rc != 0) {
    ERROR("failed to get GPU memory information: %s", strerror(rc));
    exit(rc);
  }
  attached = parse_response(rbuf, nullptr);
  used = get_msg_data<size_t>(attached, rpos);
  total = get_msg_data<size_t>(attached, rpos);

  return std::make_pair(total - used, total);
}

/**
 * send memory allocate/free information to Pod manager/scheduler
 * @param bytes memory size
 * @param is_allocate 1 for allocation, 0 for free
 * @return request succeed or not
 */
int update_memory_usage(size_t bytes, int is_allocate) {
  char sbuf[REQ_MSG_LEN], rbuf[RSP_MSG_LEN], *attached;
  size_t rpos = 0;
  int rc;
  int verdict;

  bzero(sbuf, REQ_MSG_LEN);
  prepare_request(sbuf, REQ_MEM_UPDATE, bytes, is_allocate);

  int device = get_current_device_id();
  // get verdict from Pod manager
  rc = communicate(sbuf, rbuf, NET_OP_RETRY_INTV, device);
  if (rc != 0) {
    ERROR("failed to update GPU memory usage: %s", strerror(rc));
    exit(rc);
  }
  attached = parse_response(rbuf, nullptr);
  verdict = get_msg_data<int>(attached, rpos);

  return verdict;
}

/**
 * estimate the length of a complete burst
 * @param measured_burst the length of a kernel burst measured by Predictor
 * @param measured_window the length of a window period measured by Predictor
 * @return estimated length of a complete burst
 */
double estimate_full_burst(double measured_burst, double measured_window) {
  double full_burst;

  if (measured_burst < 1e-9) {
    // no valid burst data
    full_burst = 0.0;
  } else {
    full_burst = measured_burst;
    // If application is actively using GPU, we might have a incomplete burst.
    // Therefore we increase the estimated burst time.
    if (measured_window < SCHD_OVERHEAD) full_burst *= 2;  // '2' can be changed to any value > 1
  }

  int device = get_current_device_id();
  pid_t pid = getpid();
  DEBUG("measured burst on device %d: %.3f ms, window: %.3f ms, estimated full burst: %.3f ms, PID: %d", device, measured_burst,
        measured_window, full_burst, pid);

  return full_burst;
}

/**
 * send token request to scheduling system
 * @param next_burst predicted kernel burst (milliseconds)
 * @return received time quota (milliseconds)
 */
double get_token_from_scheduler(double next_burst, int device) {
  char sbuf[REQ_MSG_LEN], rbuf[RSP_MSG_LEN], *attached;
  size_t rpos = 0;
  int rc;
  double new_quota;

  bzero(sbuf, REQ_MSG_LEN);
  prepare_request(sbuf, REQ_QUOTA, overuse, next_burst);

  // retrieve token from scheduler
  rc = communicate(sbuf, rbuf, 0, device);
  if (rc != 0) {
    ERROR("failed to get token from scheduler: %s", strerror(rc));
    exit(rc);
  }
  attached = parse_response(rbuf, nullptr);
  //new_quota = get_msg_data<double>(attached, rpos);
  new_quota = 6000;

  //DEBUG("Get token from scheduler, quota: %f", new_quota);
  DEBUG("Get token from scheduler, quota: %f", new_quota);
  return new_quota;
}

/**
 * wait for all active kernels to complete, and update overuse statistics. note that cuda default
 * stream has an additional characteristic of implicit synchronization, which roughly means that a
 * CUDA operation issued into the default stream will not begin executing until all prior issued
 * CUDA activity to that device has completed. (from https://stackoverflow.com/a/49331700 by Robert
 * Crovella)
 * @param args not in use now
 */
void *wait_cuda_kernels(void *args) {
  struct timespec ts;
  double nsec;
  while (true) {
    int device = get_current_device_id();

    // wait for tracking request
    pthread_mutex_lock(&overuse_trk_mutex);
    pthread_cond_wait(&overuse_trk_strt_cond, &overuse_trk_mutex);
    pthread_mutex_unlock(&overuse_trk_mutex);

    // calculate token expiration time
    nsec = std::max(quota_time[device], 0.0) * 1e6;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += floor(nsec / 1e9);
    ts.tv_nsec += (nsec - floor(nsec / 1e9) * 1e9);
    ts.tv_sec += ts.tv_nsec / 1000000000;
    ts.tv_nsec %= 1000000000;

    // sleep until token expired or being notified
    pthread_mutex_lock(&overuse_trk_mutex);
    int rc = pthread_cond_timedwait(&overuse_trk_intr_cond, &overuse_trk_mutex, &ts);
    if (rc != ETIMEDOUT) DEBUG("overuse tracking thread interrupted");
    pthread_mutex_unlock(&overuse_trk_mutex);

    // synchronize all running kernels
    cudaEvent_t event;
    cudaEventCreate(&event);
    cudaEventRecord(event);
    cudaEventSynchronize(event);

    // notify predictor we've done a synchronize
    host_sync_call("overuse measurement");

    float elapsed_ms;
    cudaEventElapsedTime(&elapsed_ms, cuevent_start[device], event);
    overuse[device] = std::max(0.0, (double)elapsed_ms - quota_time[device]);

    DEBUG("overuse: %.3f ms", overuse[device]);

    // notify tracking complete
    pthread_mutex_lock(&overuse_trk_mutex);
    overuse_trk_cmpl = true;
    pthread_cond_broadcast(&overuse_trk_cmpl_cond);
    pthread_mutex_unlock(&overuse_trk_mutex);
  }
  pthread_exit(NULL);
}

/**
 * pre-hooks and post-hooks
 */

CUresult cuLaunchKernel_prehook(CUfunction f, unsigned int gridDimX, unsigned int gridDimY,
                                unsigned int gridDimZ, unsigned int blockDimX,
                                unsigned int blockDimY, unsigned int blockDimZ,
                                unsigned int sharedMemBytes, CUstream hStream, void **kernelParams,
                                void **extra) {
  double new_quota, next_burst;
  int device = get_current_device_id();
  pid_t pid = getpid();
  DEBUG("hook kernel on device %d, pid: %d", device, pid);

  window_predictor[device].record_stop();

  pthread_mutex_lock(&expiration_status_mutex);
  // allow the kernel to launch if kernel burst already begins;
  // otherwise, obtain a new token if this kernel burst may cause overuse
  if (!burst_predictor[device].ongoing_unmerged() &&
      us_since(request_start) / 1e3 + burst_predictor[device].predict_unmerged() >=
          quota_time[device]) {
    // estimate the duration of next kernel burst (merged)
    next_burst = estimate_full_burst(burst_predictor[device].predict_merged(),
                                     window_predictor[device].predict_merged());

    // wait for all kernels finish
    pthread_mutex_lock(&overuse_trk_mutex);
    if (!overuse_trk_cmpl) {
      // notify overuse tracking thread to perform sync eariler
      pthread_cond_signal(&overuse_trk_intr_cond);
      pthread_cond_wait(&overuse_trk_cmpl_cond, &overuse_trk_mutex);
    }
    pthread_mutex_unlock(&overuse_trk_mutex);

    // interrupt the window which is started when overuse tracking completes
    window_predictor[device].interrupt();

    new_quota = get_token_from_scheduler(next_burst, device);

    // ensure predicted kernel burst is always less than quota
    burst_predictor[device].set_upperbound(new_quota - 1.0);

    cudaEventRecord(cuevent_start[device], 0);
    clock_gettime(CLOCK_MONOTONIC, &request_start);  // time

    quota_time[device] = new_quota;

    // wake overuse tracking thread up
    pthread_mutex_lock(&overuse_trk_mutex);
    overuse_trk_cmpl = false;
    pthread_cond_signal(&overuse_trk_strt_cond);
    pthread_mutex_unlock(&overuse_trk_mutex);
  }
  burst_predictor[device].record_start();
  pthread_mutex_unlock(&expiration_status_mutex);

  return CUDA_SUCCESS;
}

CUresult cuLaunchCooperativeKernel_prehook(CUfunction f, unsigned int gridDimX,
                                           unsigned int gridDimY, unsigned int gridDimZ,
                                           unsigned int blockDimX, unsigned int blockDimY,
                                           unsigned int blockDimZ, unsigned int sharedMemBytes,
                                           CUstream hStream, void **kernelParams) {
  return cuLaunchKernel_prehook(f, gridDimX, gridDimY, gridDimZ, blockDimX, blockDimY, blockDimZ,
                                sharedMemBytes, hStream, kernelParams, NULL);
}

// update memory usage
CUresult cuMemFree_prehook(CUdeviceptr ptr) {
  int device = get_current_device_id();
  pthread_mutex_lock(&allocation_mutex);
  if (allocation_map[device].find(ptr) == allocation_map[device].end()) {
    DEBUG("Freeing unknown memory! %zx", ptr);
  } else {
    gpu_mem_used[device] -= allocation_map[device][ptr];
    update_memory_usage(allocation_map[device][ptr], 0);
    allocation_map[device].erase(ptr);
  }
  pthread_mutex_unlock(&allocation_mutex);
  return CUDA_SUCCESS;
}

CUresult cuArrayDestroy_prehook(CUarray hArray) { return cuMemFree_prehook((CUdeviceptr)hArray); }

CUresult cuMipmappedArrayDestroy_prehook(CUmipmappedArray hMipmappedArray) {
  return cuMemFree_prehook((CUdeviceptr)hMipmappedArray);
}

// ask backend whether there's enough memory or not
CUresult cuMemAlloc_prehook(CUdeviceptr *dptr, size_t bytesize) {
  size_t remain, limit;

  std::tie(remain, limit) = get_gpu_memory_info();

  // block allocation request before over-allocate
  if (bytesize > remain) {
    ERROR("Allocate too much memory! (request: %lu B, remain: %lu B)", bytesize, remain);
    return CUDA_ERROR_OUT_OF_MEMORY;
  }

  return CUDA_SUCCESS;
}

// push memory allocation information to backend
CUresult cuMemAlloc_posthook(CUdeviceptr *dptr, size_t bytesize) {
  int device = get_current_device_id();
  // send memory usage update to backend
  if (!update_memory_usage(bytesize, 1)) {
    ERROR("Allocate too much memory!");
    return CUDA_ERROR_OUT_OF_MEMORY;
  }

  pthread_mutex_lock(&allocation_mutex);
  allocation_map[device][*dptr] = bytesize;
  gpu_mem_used[device] += bytesize;
  pthread_mutex_unlock(&allocation_mutex);

  return CUDA_SUCCESS;
}

CUresult cuMemAllocManaged_prehook(CUdeviceptr *dptr, size_t bytesize, unsigned int flags) {
  // TODO: This function access the unified memory. Behavior needs clarification.
  return CUDA_SUCCESS;
}

CUresult cuMemAllocManaged_posthook(CUdeviceptr *dptr, size_t bytesize, unsigned int flags) {
  // TODO: This function access the unified memory. Behavior needs clarification.
  return CUDA_SUCCESS;
}

CUresult cuMemAllocPitch_prehook(CUdeviceptr *dptr, size_t *pPitch, size_t WidthInBytes,
                                 size_t Height, unsigned int ElementSizeBytes) {
  return cuMemAlloc_prehook(dptr, (*pPitch) * Height);
}
CUresult cuMemAllocPitch_posthook(CUdeviceptr *dptr, size_t *pPitch, size_t WidthInBytes,
                                  size_t Height, unsigned int ElementSizeBytes) {
  return cuMemAlloc_posthook(dptr, (*pPitch) * Height);
}

inline size_t CUarray_format_to_size_t(CUarray_format Format) {
  switch (Format) {
    case CU_AD_FORMAT_UNSIGNED_INT8:
    case CU_AD_FORMAT_SIGNED_INT8:
      return 1;
    case CU_AD_FORMAT_UNSIGNED_INT16:
    case CU_AD_FORMAT_SIGNED_INT16:
    case CU_AD_FORMAT_HALF:
      return 2;
    case CU_AD_FORMAT_UNSIGNED_INT32:
    case CU_AD_FORMAT_SIGNED_INT32:
    case CU_AD_FORMAT_FLOAT:
      return 4;
  }
}

CUresult cuArrayCreate_prehook(CUarray *pHandle, const CUDA_ARRAY_DESCRIPTOR *pAllocateArray) {
  size_t totalMemoryNumber =
      pAllocateArray->Width * pAllocateArray->Height * pAllocateArray->NumChannels;
  size_t formatSize = CUarray_format_to_size_t(pAllocateArray->Format);
  return cuMemAlloc_prehook((CUdeviceptr *)pHandle, totalMemoryNumber * formatSize);
}

CUresult cuArrayCreate_posthook(CUarray *pHandle, const CUDA_ARRAY_DESCRIPTOR *pAllocateArray) {
  size_t totalMemoryNumber =
      pAllocateArray->Width * pAllocateArray->Height * pAllocateArray->NumChannels;
  size_t formatSize = CUarray_format_to_size_t(pAllocateArray->Format);
  return cuMemAlloc_posthook((CUdeviceptr *)pHandle, totalMemoryNumber * formatSize);
}

CUresult cuArray3DCreate_prehook(CUarray *pHandle, const CUDA_ARRAY3D_DESCRIPTOR *pAllocateArray) {
  size_t totalMemoryNumber = pAllocateArray->Width * pAllocateArray->Height *
                             pAllocateArray->Depth * pAllocateArray->NumChannels;
  size_t formatSize = CUarray_format_to_size_t(pAllocateArray->Format);
  return cuMemAlloc_prehook((CUdeviceptr *)pHandle, totalMemoryNumber * formatSize);
}

CUresult cuArray3DCreate_posthook(CUarray *pHandle, const CUDA_ARRAY3D_DESCRIPTOR *pAllocateArray) {
  size_t totalMemoryNumber = pAllocateArray->Width * pAllocateArray->Height *
                             pAllocateArray->Depth * pAllocateArray->NumChannels;
  size_t formatSize = CUarray_format_to_size_t(pAllocateArray->Format);
  return cuMemAlloc_posthook((CUdeviceptr *)pHandle, totalMemoryNumber * formatSize);
}

CUresult cuMipmappedArrayCreate_prehook(CUmipmappedArray *pHandle,
                                        const CUDA_ARRAY3D_DESCRIPTOR *pMipmappedArrayDesc,
                                        unsigned int numMipmapLevels) {
  // TODO: check mipmap array size
  return CUDA_SUCCESS;
}

CUresult cuMipmappedArrayCreate_posthook(CUmipmappedArray *pHandle,
                                         const CUDA_ARRAY3D_DESCRIPTOR *pMipmappedArrayDesc,
                                         unsigned int numMipmapLevels) {
  // TODO: check mipmap array size
  return CUDA_SUCCESS;
}

CUresult cuCtxSynchronize_posthook(void) {
  host_sync_call("cuCtxSynchronize");
  return CUDA_SUCCESS;
}

CUresult cuMemcpyAtoH_posthook(void *dstHost, CUarray srcArray, size_t srcOffset,
                               size_t ByteCount) {
  host_sync_call("cuMemcpyAtoH");
  return CUDA_SUCCESS;
}

CUresult cuMemcpyDtoH_posthook(void *dstHost, CUdeviceptr srcDevice, size_t ByteCount) {
  host_sync_call("cuMemcpyDtoH");
  return CUDA_SUCCESS;
}

CUresult cuMemcpyHtoA_posthook(CUarray dstArray, size_t dstOffset, const void *srcHost,
                               size_t ByteCount) {
  host_sync_call("cuMemcpyHtoA");
  return CUDA_SUCCESS;
}

CUresult cuMemcpyHtoD_posthook(CUarray dstArray, size_t dstOffset, const void *srcHost,
                               size_t ByteCount, CUstream hStream) {
  host_sync_call("cuMemcpyHtoD");
  return CUDA_SUCCESS;
}

void initialize() {
  // Init all variable in array
  for (int i = 0; i < max_gpu_num; i++) {
    quota_time[i] = 0.0;
  }

  // place post-hooks
  hook_inf.postHooks[CU_HOOK_MEMCPY_ATOH] = (void *)cuMemcpyAtoH_posthook;
  hook_inf.postHooks[CU_HOOK_MEMCPY_DTOH] = (void *)cuMemcpyDtoH_posthook;
  hook_inf.postHooks[CU_HOOK_MEMCPY_HTOA] = (void *)cuMemcpyHtoA_posthook;
  hook_inf.postHooks[CU_HOOK_MEMCPY_HTOD] = (void *)cuMemcpyHtoD_posthook;
  hook_inf.postHooks[CU_HOOK_CTX_SYNC] = (void *)cuCtxSynchronize_posthook;

  hook_inf.postHooks[CU_HOOK_MEM_ALLOC] = (void *)cuMemAlloc_posthook;
  hook_inf.postHooks[CU_HOOK_MEM_ALLOC_MANAGED] = (void *)cuMemAllocManaged_posthook;
  hook_inf.postHooks[CU_HOOK_MEM_ALLOC_PITCH] = (void *)cuMemAllocPitch_posthook;
  hook_inf.postHooks[CU_HOOK_ARRAY_CREATE] = (void *)cuArrayCreate_posthook;
  hook_inf.postHooks[CU_HOOK_ARRAY3D_CREATE] = (void *)cuArray3DCreate_posthook;
  hook_inf.postHooks[CU_HOOK_MIPMAPPED_ARRAY_CREATE] = (void *)cuMipmappedArrayCreate_posthook;
  // place pre-hooks
  hook_inf.preHooks[CU_HOOK_MEM_FREE] = (void *)cuMemFree_prehook;
  hook_inf.preHooks[CU_HOOK_ARRAY_DESTROY] = (void *)cuArrayDestroy_prehook;
  hook_inf.preHooks[CU_HOOK_MIPMAPPED_ARRAY_DESTROY] = (void *)cuMipmappedArrayDestroy_prehook;
  hook_inf.preHooks[CU_HOOK_LAUNCH_KERNEL] = (void *)cuLaunchKernel_prehook;
  hook_inf.preHooks[CU_HOOK_LAUNCH_COOPERATIVE_KERNEL] = (void *)cuLaunchCooperativeKernel_prehook;

  hook_inf.preHooks[CU_HOOK_MEM_ALLOC] = (void *)cuMemAlloc_prehook;
  hook_inf.preHooks[CU_HOOK_MEM_ALLOC_MANAGED] = (void *)cuMemAllocManaged_prehook;
  hook_inf.preHooks[CU_HOOK_MEM_ALLOC_PITCH] = (void *)cuMemAllocPitch_prehook;
  hook_inf.preHooks[CU_HOOK_ARRAY_CREATE] = (void *)cuArrayCreate_prehook;
  hook_inf.preHooks[CU_HOOK_ARRAY3D_CREATE] = (void *)cuArray3DCreate_prehook;
  hook_inf.preHooks[CU_HOOK_MIPMAPPED_ARRAY_CREATE] = (void *)cuMipmappedArrayCreate_prehook;

  configure_connection();
  pthread_mutex_lock(&request_time_mutex);
  for (int i = 0; i < current_gpu_num; i++) {
      cudaEventCreate(&cuevent_start[i]);
  }

  // initialize overuse_trk_intr_cond with CLOCK_MONOTONIC
  pthread_condattr_t attr_monotonic_clock;
  pthread_condattr_init(&attr_monotonic_clock);
  pthread_condattr_setclock(&attr_monotonic_clock, CLOCK_MONOTONIC);
  pthread_cond_init(&overuse_trk_intr_cond, &attr_monotonic_clock);

  // a thread running overuse tracking
  pthread_t overuse_trk_tid;
  pthread_create(&overuse_trk_tid, NULL, wait_cuda_kernels, NULL);

  // first token request
  overuse_trk_cmpl = true;  // bypass first overuse tracking to prevent deadlock

  //Init token for every scheduler
  for (int i = 0; i < current_gpu_num; i++) {
    DEBUG("get init tocken to: %d", i);
    get_token_from_scheduler(0.0, i);
  }

  pthread_mutex_unlock(&request_time_mutex);
}

CUstream hStream;  // redundent variable used for macro expansion

#define CU_HOOK_GENERATE_INTERCEPT(hooksymbol, funcname, params, ...)                     \
  CUresult CUDAAPI funcname params {                                                      \
    pthread_once(&init_done, initialize);                                                 \
                                                                                          \
    static void *real_func = (void *)real_dlsym(RTLD_NEXT, CUDA_SYMBOL_STRING(funcname)); \
    CUresult result = CUDA_SUCCESS;                                                       \
                                                                                          \
    if (hook_inf.debug_mode) hook_inf.call_count[hooksymbol]++;                           \
                                                                                          \
    if (hook_inf.preHooks[hooksymbol])                                                    \
      result = ((CUresult CUDAAPI(*) params)hook_inf.preHooks[hooksymbol])(__VA_ARGS__);  \
    if (result != CUDA_SUCCESS) return (result);                                          \
    result = ((CUresult CUDAAPI(*) params)real_func)(__VA_ARGS__);                        \
    if (hook_inf.postHooks[hooksymbol] && result == CUDA_SUCCESS)                         \
      result = ((CUresult CUDAAPI(*) params)hook_inf.postHooks[hooksymbol])(__VA_ARGS__); \
                                                                                          \
    return (result);                                                                      \
  }

CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_MEMCPY_ATOH, cuMemcpyAtoH,
                           (void *dstHost, CUarray srcArray, size_t srcOffset, size_t ByteCount),
                           dstHost, srcArray, srcOffset, ByteCount)
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_MEMCPY_DTOH, cuMemcpyDtoH,
                           (void *dstHost, CUdeviceptr srcDevice, size_t ByteCount), dstHost,
                           srcDevice, ByteCount)
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_MEMCPY_HTOA, cuMemcpyHtoA,
                           (CUarray dstArray, size_t dstOffset, const void *srcHost,
                            size_t ByteCount),
                           dstArray, dstOffset, srcHost, ByteCount)
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_MEMCPY_HTOD, cuMemcpyHtoD,
                           (CUdeviceptr dstDevice, const void *srcHost, size_t ByteCount),
                           dstDevice, srcHost, ByteCount)

// cuda driver ctx/management
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_CTX_SYNC, cuCtxSynchronize, (void))
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_CTX_GET_CURRENT, cuCtxGetCurrent, (CUcontext * pctx), pctx)
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_CTX_SET_CURRENT, cuCtxSetCurrent, (CUcontext ctx), ctx)
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_CTX_DESTROY, cuCtxDestroy, (CUcontext ctx), ctx)
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_CTX_CREATE, cuCtxCreate,
                           (CUcontext * pctx, unsigned int flags, CUdevice dev), pctx, flags, dev)
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_CTX_POP_CURRENT, cuCtxPopCurrent, (CUcontext * pctx), pctx)
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_CTX_PUSH_CURRENT, cuCtxPushCurrent, (CUcontext ctx), ctx)
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_DEVICE_PRIMARY_CTX_RETAIN, cuDevicePrimaryCtxRetain,
                           (CUcontext * pctx, CUdevice dev), pctx, dev)
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_DEVICE_PRIMARY_CTX_RESET, cuDevicePrimaryCtxReset,
                           (CUdevice dev), dev)
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_DEVICE_PRIMARY_CTX_RELEASE, cuDevicePrimaryCtxRelease,
                           (CUdevice dev), dev)

// cuda driver alloc/free APIs
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_MEM_ALLOC, cuMemAlloc, (CUdeviceptr * dptr, size_t bytesize),
                           dptr, bytesize)
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_MEM_ALLOC_MANAGED, cuMemAllocManaged,
                           (CUdeviceptr * dptr, size_t bytesize, unsigned int flags), dptr,
                           bytesize, flags)
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_MEM_ALLOC_PITCH, cuMemAllocPitch,
                           (CUdeviceptr * dptr, size_t *pPitch, size_t WidthInBytes, size_t Height,
                            unsigned int ElementSizeBytes),
                           dptr, pPitch, WidthInBytes, Height, ElementSizeBytes)
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_MEM_FREE, cuMemFree, (CUdeviceptr dptr), dptr)

// cuda driver array/array_destroy APIs
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_ARRAY_CREATE, cuArrayCreate,
                           (CUarray * pHandle, const CUDA_ARRAY_DESCRIPTOR *pAllocateArray),
                           pHandle, pAllocateArray)
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_ARRAY3D_CREATE, cuArray3DCreate,
                           (CUarray * pHandle, const CUDA_ARRAY3D_DESCRIPTOR *pAllocateArray),
                           pHandle, pAllocateArray)
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_MIPMAPPED_ARRAY_CREATE, cuMipmappedArrayCreate,
                           (CUmipmappedArray * pHandle,
                            const CUDA_ARRAY3D_DESCRIPTOR *pMipmappedArrayDesc,
                            unsigned int numMipmapLevels),
                           pHandle, pMipmappedArrayDesc, numMipmapLevels)
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_ARRAY_DESTROY, cuArrayDestroy, (CUarray hArray), hArray)
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_MIPMAPPED_ARRAY_DESTROY, cuMipmappedArrayDestroy,
                           (CUmipmappedArray hMipmappedArray), hMipmappedArray)

// cuda driver kernel launch APIs
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_LAUNCH_KERNEL, cuLaunchKernel,
                           (CUfunction f, unsigned int gridDimX, unsigned int gridDimY,
                            unsigned int gridDimZ, unsigned int blockDimX, unsigned int blockDimY,
                            unsigned int blockDimZ, unsigned int sharedMemBytes, CUstream hStream,
                            void **kernelParams, void **extra),
                           f, gridDimX, gridDimY, gridDimZ, blockDimX, blockDimY, blockDimZ,
                           sharedMemBytes, hStream, kernelParams, extra)
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_LAUNCH_COOPERATIVE_KERNEL, cuLaunchCooperativeKernel,
                           (CUfunction f, unsigned int gridDimX, unsigned int gridDimY,
                            unsigned int gridDimZ, unsigned int blockDimX, unsigned int blockDimY,
                            unsigned int blockDimZ, unsigned int sharedMemBytes, CUstream hStream,
                            void **kernelParams),
                           f, gridDimX, gridDimY, gridDimZ, blockDimX, blockDimY, blockDimZ,
                           sharedMemBytes, hStream, kernelParams)

// cuda driver mem info APIs
CUresult CUDAAPI cuDeviceTotalMem(size_t *bytes, CUdevice dev) {
  pthread_once(&init_done, initialize);
  auto mem_info = get_gpu_memory_info();
  if (hook_inf.debug_mode) hook_inf.call_count[CU_HOOK_DEVICE_TOTOAL_MEM]++;
  *bytes = mem_info.second;
  return CUDA_SUCCESS;
}

CUresult CUDAAPI cuMemGetInfo(size_t *gpu_mem_free, size_t *gpu_mem_total) {
  pthread_once(&init_done, initialize);
  auto mem_info = get_gpu_memory_info();
  if (hook_inf.debug_mode) hook_inf.call_count[CU_HOOK_MEM_INFO]++;
  *gpu_mem_free = mem_info.first;
  *gpu_mem_total = mem_info.second;
  return CUDA_SUCCESS;
}
