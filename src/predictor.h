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

#ifndef PREDICTOR_H
#define PREDICTOR_H

#include <pthread.h>

#include <algorithm>
#include <chrono>
#include <deque>

typedef std::chrono::time_point<std::chrono::_V2::steady_clock> timepoint_t;

const size_t PREDICT_MAX_KEEP = 8;

class Predictor {
 public:
  Predictor(const char *name = "", const double thres = 0.0);
  ~Predictor();
  void record_stop();
  void record_start();
  void interrupt();
  double predict();
  void recalc();
  void reset();

 private:
  const char *name;
  // two consecutive period with interval less than this value will be merged
  const double MERGE_THRES;
  pthread_mutex_t mutex_;
  timepoint_t period_begin_;
  timepoint_t last_period_begin_, last_period_end_;
  std::deque<double> past_records_;
  double max_duration_;
};

#endif
