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
 * Kernel burst and window period measurement/prediction utilities.
 *
 * Measurement and prediction can be disabled by defining NO_PREDICT.
 */

#include "predictor.h"

#include "debug.h"

using std::make_pair;
using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::steady_clock;

Predictor::Predictor(const char *name, const double thres) : MERGE_THRES(thres) {
  mutex_ = PTHREAD_MUTEX_INITIALIZER;
  period_begin_ = timepoint_t::max();
  last_period_begin_ = timepoint_t::max();
  last_period_end_ = timepoint_t::min();
  counter_ = 0;
  this->name = name;
}

Predictor::~Predictor() { pthread_mutex_destroy(&mutex_); }

// maintains the decreasing property, for O(1) time complexity
void Predictor::add_record(double duration) {
  // maintain a decreasing list
  while (!past_records_.empty() && past_records_.back().second < duration) past_records_.pop_back();
  past_records_.push_back(make_pair(counter_, duration));
  ++counter_;
}

// Marks complete for a period
void Predictor::record_stop() {
#ifndef NO_PREDICT
  double duration;
  timepoint_t tp;

  pthread_mutex_lock(&mutex_);
  if (period_begin_ != timepoint_t::max()) {
    // record kernel burst duration
    tp = steady_clock::now();
    duration = duration_cast<microseconds>(tp - period_begin_).count() / 1e3;
    add_record(duration);
    last_period_end_ = tp;
    DEBUG("%s: record stop (length: %.3f ms)", name, duration);
  }
  period_begin_ = timepoint_t::max();
  pthread_mutex_unlock(&mutex_);
#endif
}

// Marks begin for a period. Future calls until record_stop is called takes no effect.
void Predictor::record_start() {
#ifndef NO_PREDICT
  double period_intv;
  pthread_mutex_lock(&mutex_);
  if (period_begin_ == timepoint_t::max()) {
    period_begin_ = steady_clock::now();
    period_intv = duration_cast<microseconds>(period_begin_ - last_period_end_).count() / 1e3;
    if (last_period_end_ != timepoint_t::min() && period_intv < MERGE_THRES) {
      DEBUG("%s: merge", name);
      period_begin_ = last_period_begin_;
    }

    last_period_begin_ = period_begin_;
    DEBUG("%s: record start", name);
  }
  pthread_mutex_unlock(&mutex_);
#endif
}

// Invalidate currently running period.
// A new period begins with another record_start call afterwards.
void Predictor::interrupt() {
#ifndef NO_PREDICT
  pthread_mutex_lock(&mutex_);
  period_begin_ = timepoint_t::max();
  last_period_begin_ = timepoint_t::max();
  last_period_end_ = timepoint_t::min();
  DEBUG("%s: interrupted", name);
  pthread_mutex_unlock(&mutex_);
#endif
}

double Predictor::predict() {
#ifndef NO_PREDICT
  double ret = 0;
  pthread_mutex_lock(&mutex_);
  // first one is the maximum
  if (!past_records_.empty()) ret = past_records_.front().second;
  pthread_mutex_unlock(&mutex_);
  return ret;
#else
  return 0;
#endif
}

// Trim past records
void Predictor::recalc() {
#ifndef NO_PREDICT
  pthread_mutex_lock(&mutex_);
  while (!past_records_.empty() && counter_ - past_records_.front().first > PREDICT_MAX_KEEP)
    past_records_.pop_front();
  DEBUG("%s: recalc = %.3f", name, past_records_.front().second);
  pthread_mutex_unlock(&mutex_);
#endif
}

// Clear all past records and status
void Predictor::reset() {
#ifndef NO_PREDICT
  pthread_mutex_lock(&mutex_);
  past_records_.clear();
  period_begin_ = timepoint_t::max();
  last_period_begin_ = timepoint_t::max();
  last_period_end_ = timepoint_t::min();
  counter_ = 0;
  pthread_mutex_unlock(&mutex_);
#endif
}
