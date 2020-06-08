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
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <random>
#include <sstream>
#include <unordered_map>

#include "comm/endpoint.hpp"
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
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemcpyAtoHAsync)) == 0) {
    return (void *)(&cuMemcpyAtoHAsync);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemcpyDtoH)) == 0) {
    return (void *)(&cuMemcpyDtoH);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemcpyDtoHAsync)) == 0) {
    return (void *)(&cuMemcpyDtoHAsync);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemcpyHtoA)) == 0) {
    return (void *)(&cuMemcpyHtoA);
  } else if (strcmp(symbol, CUDA_SYMBOL_STRING(cuMemcpyHtoD)) == 0) {
    return (void *)(&cuMemcpyHtoD);
  }
  // omit cuDeviceTotalMem here so there won't be a deadlock in cudaEventCreate when we are in
  // initialize(). Functions called by client are still being intercepted.
  return (real_dlsym(handle, symbol));
}

class BurstProgress {
 private:
  std::mutex mutex_;
  std::unordered_map<CUfunction, uint32_t> burst_kernel_count_;
  CUfunction start_func_;
  uint32_t launch_count_;

 public:
  BurstProgress() {
    start_func_ = nullptr;
    launch_count_ = 0;
  }

  ~BurstProgress() {}

  bool started() {
    std::lock_guard<std::mutex> lg(mutex_);
    return start_func_ != nullptr;
  }

  void setBurstStartFunc(CUfunction f) {
    std::lock_guard<std::mutex> lg(mutex_);
    start_func_ = f;
  }

  // get current progress; assume each kernel in a kernel burst has same duration.
  double currentProgress() {
    std::lock_guard<std::mutex> lg(mutex_);
    // if no burst measured, currentProgress will always return 0, so hook never early stops
    if (start_func_ == nullptr ||
        burst_kernel_count_.find(start_func_) == burst_kernel_count_.end()) {
      return 0.0;
    }
    return launch_count_ / (double)burst_kernel_count_[start_func_];
  }

  // increase counter
  void step() {
    std::lock_guard<std::mutex> lg(mutex_);
    launch_count_++;
  }

  // save last record and restart counter
  void reset() {
    std::lock_guard<std::mutex> lg(mutex_);
    if (start_func_ == nullptr) return;
    if (burst_kernel_count_.find(start_func_) == burst_kernel_count_.end()) {
      burst_kernel_count_[start_func_] = launch_count_;
    } else {
      // exponential decay (new 3 : 1 old)
      burst_kernel_count_[start_func_] = (burst_kernel_count_[start_func_] + 3 * launch_count_) / 4;
    }
    start_func_ = nullptr;
    launch_count_ = 0;
  }
};

/* connection with backend */
char client_random_id[9];                                // hex string of a 32-bit random number
pthread_mutex_t comm_mutex = PTHREAD_MUTEX_INITIALIZER;  // one communication at a time
Requester *requester;

/* GPU computation resource usage */
double quota_time = 0;  // time quota from scheduler
double overuse = 0;     // overuse time (ms)
pthread_mutex_t request_time_mutex = PTHREAD_MUTEX_INITIALIZER;

// predictors
const double SCHD_OVERHEAD = 2.0;                   // ms
Predictor burst_predictor("burst", SCHD_OVERHEAD);  // predicted burst may not be the full burst
Predictor window_predictor("window");
BurstProgress burst_progress;

pthread_mutex_t expiration_status_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_once_t init_done = PTHREAD_ONCE_INIT;
cudaEvent_t cuevent_start;      // the time receive new token
struct timespec request_start;  // the time receive new token

pthread_mutex_t overuse_trk_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t overuse_trk_strt_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t overuse_trk_cmpl_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t overuse_trk_intr_cond;  // initialize it with CLOCK_MONOTONIC
bool overuse_trk_cmpl;

// GPU memory allocation information
pthread_mutex_t allocation_mutex = PTHREAD_MUTEX_INITIALIZER;
std::map<CUdeviceptr, size_t> allocation_map;
size_t gpu_mem_used = 0;  // local accounting only

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
 * Set up connection to backend with information provided in environment variables.
 */
void configure_connection() {
  char group_name[HOST_NAME_MAX], ipc_dir[PATH_MAX] = "/tmp/gemini/ipc";

  // get directory for IPC files
  if (getenv("GEMINI_IPC_DIR") != nullptr) {
    strncpy(ipc_dir, getenv("GEMINI_IPC_DIR"), sizeof(ipc_dir));
  }

  if (getenv("GEMINI_GROUP_NAME") != nullptr) {
    strcpy(group_name, getenv("GEMINI_GROUP_NAME"));
  } else {
    // use host name as group name
    gethostname(group_name, HOST_NAME_MAX);
  }

  char url[PATH_MAX];
  snprintf(url, PATH_MAX, "ipc://%s/%s", ipc_dir, group_name);
  requester = new Requester(nullptr, url);
  DEBUG("Connected to %s", url);
}

/**
 * Synchronize with a CUDA event before host-side synchronous function, in order to capture the
 * kernel burst length. Since we want kernel burst to contain the time of kernel execution only, we
 * stop predictor recording before the sync operation starts. Therefore, time taken by sync
 * operation will not be counted in kernel burst time, such as memory copy time.
 * @param func_name name of synchronous call
 */
void preHostSyncCall(const char *func_name, cudaEvent_t event = nullptr) {
  float elapsed_time = 0;
  bool recycle = (event == nullptr);
  if (recycle) cudaEventCreate(&event);
  // force a synchronization
  cudaEventRecord(event);
  cudaEventSynchronize(event);
  // destroy the object if no longer needed
  if (recycle) cudaEventDestroy(event);
#ifdef SYNCP_MESSAGE
  DEBUG("PRE_SYNC (%s)", func_name);
#endif
  burst_predictor.record_stop();
  burst_progress.reset();
}

/**
 * Start recording window period after host-side synchronous operation completes.
 * @param func_name name of synchronous call
 */
void postHostSyncCall(const char *func_name) {
#ifdef SYNCP_MESSAGE
  DEBUG("POST_SYNC (%s)", func_name);
#endif
  window_predictor.record_start();
}

/**
 * get available GPU memory from backend.
 * assume configured memory limit won't exceed hardware limit
 * @return remaining memory, memory limit
 */
std::pair<size_t, size_t> get_gpu_memory_info() {
  MemInfoRequest request(client_random_id);
  MemInfoResponse response;
  requester->submit(request, &response);
  return std::make_pair(response.total() - response.used(), response.total());
}

/**
 * send memory allocate/free information to backend
 * @param bytes memory size
 * @param is_allocate
 * @return operation permitted or not
 */
int update_memory_usage(size_t bytes, bool is_allocate) {
  MemAllocRequest request(client_random_id, bytes, is_allocate);
  MemAllocResponse response;
  requester->submit(request, &response);
  return response.permitted();
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

  DEBUG("measured burst: %.3f ms, window: %.3f ms, estimated full burst: %.3f ms", measured_burst,
        measured_window, full_burst);

  return full_burst;
}

/**
 * ask token from backend system
 * @param next_burst predicted kernel burst (milliseconds)
 * @return received time quota (milliseconds)
 */
double get_token_from_scheduler(double next_burst) {
  TokenRequest request(client_random_id, overuse, next_burst);
  TokenResponse response;
  requester->submit(request, &response);
  return response.quota();
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
    // wait for tracking request
    pthread_mutex_lock(&overuse_trk_mutex);
    pthread_cond_wait(&overuse_trk_strt_cond, &overuse_trk_mutex);
    pthread_mutex_unlock(&overuse_trk_mutex);

    // calculate token expiration time
    nsec = std::max(quota_time, 0.0) * 1e6;
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

    // block until preceding kernel completes, and update kernel burst & window statistics
    cudaEvent_t event;
    cudaEventCreate(&event);
    preHostSyncCall("overuse measurement", event);
    postHostSyncCall("overuse measurement");

    // calculate overuse time
    float elapsed_ms;
    cudaEventElapsedTime(&elapsed_ms, cuevent_start, event);
    cudaEventDestroy(event);
    overuse = std::max(0.0, (double)elapsed_ms - quota_time);

    DEBUG("overuse: %.3f ms", overuse);

    // notify tracking complete
    pthread_mutex_lock(&overuse_trk_mutex);
    overuse_trk_cmpl = true;
    pthread_cond_broadcast(&overuse_trk_cmpl_cond);
    pthread_mutex_unlock(&overuse_trk_mutex);
  }
  pthread_exit(NULL);
}

// A background thread sending & receiving heartbeat message from backend
void *heartbeatThread(void *) {
  const int kHeartbeatIntv = 5;
  while (true) {
    HeartbeatRequest request(client_random_id);
    requester->submit(request, nullptr);
    sleep(kHeartbeatIntv);
  }
  pthread_exit(nullptr);
}

// Free up memory usage of current process accounted in backend when current process terminates
void releaseMemoryOnTerminate() {
  MemAllocRequest request(client_random_id, gpu_mem_used, false);
  MemAllocResponse response;
  requester->submit(request, &response);
  if (response.permitted()) {
    INFO("all accounted GPU memory usage (%lu B) are released in backend", gpu_mem_used);
  } else {
    ERROR("failed to release accounted memory usage in backend on process termination");
  }
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

  window_predictor.record_stop();

  pthread_mutex_lock(&expiration_status_mutex);

  // increase progress counter first, and see if this kernel launch will cause overuse
  if (!burst_progress.started()) {
    burst_progress.setBurstStartFunc(f);
  }
  burst_progress.step();
  
  // obtain a new token if this kernel may cause overuse
  if (us_since(request_start) / 1e3 +
          burst_predictor.predict_unmerged() * burst_progress.currentProgress() >=
      quota_time) {
    // estimate the duration of next kernel burst (merged)
    next_burst =
        estimate_full_burst(burst_predictor.predict_merged(), window_predictor.predict_merged());

    // wait for all kernels finish
    pthread_mutex_lock(&overuse_trk_mutex);
    if (!overuse_trk_cmpl) {
      // notify overuse tracking thread to perform sync eariler
      pthread_cond_signal(&overuse_trk_intr_cond);
      pthread_cond_wait(&overuse_trk_cmpl_cond, &overuse_trk_mutex);
    }
    pthread_mutex_unlock(&overuse_trk_mutex);

    // interrupt the window which is started when overuse tracking completes
    window_predictor.interrupt();

    new_quota = get_token_from_scheduler(next_burst);

    // ensure predicted kernel burst is always less than quota
    burst_predictor.set_upperbound(new_quota - 1.0);

    cudaEventRecord(cuevent_start, 0);
    clock_gettime(CLOCK_MONOTONIC, &request_start);  // time

    quota_time = new_quota;

    // wake overuse tracking thread up
    pthread_mutex_lock(&overuse_trk_mutex);
    overuse_trk_cmpl = false;
    pthread_cond_signal(&overuse_trk_strt_cond);
    pthread_mutex_unlock(&overuse_trk_mutex);
  }
  burst_predictor.record_start();
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
  pthread_mutex_lock(&allocation_mutex);
  if (allocation_map.find(ptr) == allocation_map.end()) {
    DEBUG("Freeing unknown memory! %zx", ptr);
  } else {
    gpu_mem_used -= allocation_map[ptr];
    update_memory_usage(allocation_map[ptr], false);
    allocation_map.erase(ptr);
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
  // send memory usage update to backend
  if (!update_memory_usage(bytesize, true)) {
    ERROR("Allocate too much memory!");
    return CUDA_ERROR_OUT_OF_MEMORY;
  }

  pthread_mutex_lock(&allocation_mutex);
  allocation_map[*dptr] = bytesize;
  gpu_mem_used += bytesize;
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

CUresult cuCtxSynchronize_prehook(void) {
  preHostSyncCall("cuCtxSynchronize");
  return CUDA_SUCCESS;
}

CUresult cuCtxSynchronize_posthook(void) {
  postHostSyncCall("cuCtxSynchronize");
  return CUDA_SUCCESS;
}

CUresult cuMemcpyAtoH_prehook(void *dstHost, CUarray srcArray, size_t srcOffset, size_t ByteCount) {
  preHostSyncCall("cuMemcpyAtoH");
  return CUDA_SUCCESS;
}

CUresult cuMemcpyAtoH_posthook(void *dstHost, CUarray srcArray, size_t srcOffset,
                               size_t ByteCount) {
  postHostSyncCall("cuMemcpyAtoH");
  return CUDA_SUCCESS;
}

CUresult cuMemcpyDtoH_prehook(void *dstHost, CUdeviceptr srcDevice, size_t ByteCount) {
  preHostSyncCall("cuMemcpyDtoH");
  return CUDA_SUCCESS;
}

CUresult cuMemcpyDtoH_posthook(void *dstHost, CUdeviceptr srcDevice, size_t ByteCount) {
  postHostSyncCall("cuMemcpyDtoH");
  return CUDA_SUCCESS;
}

CUresult cuMemcpyHtoA_prehook(CUarray dstArray, size_t dstOffset, const void *srcHost,
                              size_t ByteCount) {
  preHostSyncCall("cuMemcpyHtoA");
  return CUDA_SUCCESS;
}

CUresult cuMemcpyHtoA_posthook(CUarray dstArray, size_t dstOffset, const void *srcHost,
                               size_t ByteCount) {
  postHostSyncCall("cuMemcpyHtoA");
  return CUDA_SUCCESS;
}

CUresult cuMemcpyHtoD_prehook(CUarray dstArray, size_t dstOffset, const void *srcHost,
                              size_t ByteCount, CUstream hStream) {
  preHostSyncCall("cuMemcpyHtoD");
  return CUDA_SUCCESS;
}

CUresult cuMemcpyHtoD_posthook(CUarray dstArray, size_t dstOffset, const void *srcHost,
                               size_t ByteCount, CUstream hStream) {
  postHostSyncCall("cuMemcpyHtoD");
  return CUDA_SUCCESS;
}

void initialize() {
  // generate random client identifier
  srand(getpid() ^ time(nullptr));
  snprintf(client_random_id, sizeof(client_random_id), "%08x", rand());

  // host-side synchronous calls
  hook_inf.preHooks[CU_HOOK_MEMCPY_ATOH] = (void *)cuMemcpyAtoH_prehook;
  hook_inf.preHooks[CU_HOOK_MEMCPY_DTOH] = (void *)cuMemcpyDtoH_prehook;
  hook_inf.preHooks[CU_HOOK_MEMCPY_HTOA] = (void *)cuMemcpyHtoA_prehook;
  hook_inf.preHooks[CU_HOOK_MEMCPY_HTOD] = (void *)cuMemcpyHtoD_prehook;
  hook_inf.preHooks[CU_HOOK_CTX_SYNC] = (void *)cuCtxSynchronize_prehook;
  hook_inf.postHooks[CU_HOOK_MEMCPY_ATOH] = (void *)cuMemcpyAtoH_posthook;
  hook_inf.postHooks[CU_HOOK_MEMCPY_DTOH] = (void *)cuMemcpyDtoH_posthook;
  hook_inf.postHooks[CU_HOOK_MEMCPY_HTOA] = (void *)cuMemcpyHtoA_posthook;
  hook_inf.postHooks[CU_HOOK_MEMCPY_HTOD] = (void *)cuMemcpyHtoD_posthook;
  hook_inf.postHooks[CU_HOOK_CTX_SYNC] = (void *)cuCtxSynchronize_posthook;

  // memory allocation
  hook_inf.preHooks[CU_HOOK_MEM_ALLOC] = (void *)cuMemAlloc_prehook;
  hook_inf.preHooks[CU_HOOK_MEM_ALLOC_MANAGED] = (void *)cuMemAllocManaged_prehook;
  hook_inf.preHooks[CU_HOOK_MEM_ALLOC_PITCH] = (void *)cuMemAllocPitch_prehook;
  hook_inf.preHooks[CU_HOOK_ARRAY_CREATE] = (void *)cuArrayCreate_prehook;
  hook_inf.preHooks[CU_HOOK_ARRAY3D_CREATE] = (void *)cuArray3DCreate_prehook;
  hook_inf.preHooks[CU_HOOK_MIPMAPPED_ARRAY_CREATE] = (void *)cuMipmappedArrayCreate_prehook;
  hook_inf.postHooks[CU_HOOK_MEM_ALLOC] = (void *)cuMemAlloc_posthook;
  hook_inf.postHooks[CU_HOOK_MEM_ALLOC_MANAGED] = (void *)cuMemAllocManaged_posthook;
  hook_inf.postHooks[CU_HOOK_MEM_ALLOC_PITCH] = (void *)cuMemAllocPitch_posthook;
  hook_inf.postHooks[CU_HOOK_ARRAY_CREATE] = (void *)cuArrayCreate_posthook;
  hook_inf.postHooks[CU_HOOK_ARRAY3D_CREATE] = (void *)cuArray3DCreate_posthook;
  hook_inf.postHooks[CU_HOOK_MIPMAPPED_ARRAY_CREATE] = (void *)cuMipmappedArrayCreate_posthook;

  // memory deallocation
  hook_inf.preHooks[CU_HOOK_MEM_FREE] = (void *)cuMemFree_prehook;
  hook_inf.preHooks[CU_HOOK_ARRAY_DESTROY] = (void *)cuArrayDestroy_prehook;
  hook_inf.preHooks[CU_HOOK_MIPMAPPED_ARRAY_DESTROY] = (void *)cuMipmappedArrayDestroy_prehook;

  // kernel execution
  hook_inf.preHooks[CU_HOOK_LAUNCH_KERNEL] = (void *)cuLaunchKernel_prehook;
  hook_inf.preHooks[CU_HOOK_LAUNCH_COOPERATIVE_KERNEL] = (void *)cuLaunchCooperativeKernel_prehook;

  configure_connection();

  // register memory usage clean-up function
  atexit(releaseMemoryOnTerminate);

  // launch heartbeat thread
  pthread_t tid;
  pthread_create(&tid, nullptr, heartbeatThread, nullptr);
  pthread_detach(tid);

  pthread_mutex_lock(&request_time_mutex);
  cudaEventCreate(&cuevent_start);

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
  get_token_from_scheduler(0.0);

  pthread_mutex_unlock(&request_time_mutex);
}

// CUstream hStream;  // redundent variable used for macro expansion

#define CU_HOOK_GENERATE_INTERCEPT(hooksymbol, funcname, params, ...)                     \
  CUresult CUDAAPI funcname params {                                                      \
    if (hook_inf.debug_mode) DEBUG("hooked function: " CUDA_SYMBOL_STRING(hooksymbol));   \
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
                                                                                          \
    result = ((CUresult CUDAAPI(*) params)real_func)(__VA_ARGS__);                        \
                                                                                          \
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
CU_HOOK_GENERATE_INTERCEPT(CU_HOOK_CTX_SYNC, cuCtxSynchronize, (void))

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
