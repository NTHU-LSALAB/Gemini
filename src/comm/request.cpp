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

#include "request.hpp"

#include <cstring>

Request::Request() : Message() {}

Request::Request(vector<zmq_msg_t> &zmq_frames) : Message(zmq_frames) {}

Request::Request(const char *client_name) : Message() {
  addFrame(client_name, strnlen(client_name, kMaxClientNameLen) + 1);
}

Request::Request(Request &request) : Message() {
  clear();
  for (pair<char *, size_t> p : request.frames_) {
    addFrame(p.first, p.second);
  }
}

Request::~Request() {}

string Request::from() { return string(frames_[1].first); }

KernelTokenRequest::KernelTokenRequest(const char *client_name, double overuse, double next_burst)
    : Request(client_name) {
  setType(kKernelToken);
  addFrame(&overuse, sizeof(double));
  addFrame(&next_burst, sizeof(double));
}

KernelTokenRequest::KernelTokenRequest(Request &base_request) : Request(base_request) {
  assert(what() == kKernelToken);
}

double KernelTokenRequest::overuse() { return *reinterpret_cast<double *>(frames_[2].first); }

double KernelTokenRequest::nextBurst() { return *reinterpret_cast<double *>(frames_[3].first); }

PrefetchTokenRequest::PrefetchTokenRequest(const char *client_name, double overuse,
                                           double next_burst)
    : Request(client_name) {
  setType(kPrefetchToken);
  addFrame(&overuse, sizeof(double));
  addFrame(&next_burst, sizeof(double));
}

PrefetchTokenRequest::PrefetchTokenRequest(Request &base_request) : Request(base_request) {
  assert(what() == kPrefetchToken);
}

double PrefetchTokenRequest::overuse() { return *reinterpret_cast<double *>(frames_[2].first); }

double PrefetchTokenRequest::nextBurst() { return *reinterpret_cast<double *>(frames_[3].first); }

PrefetchRequest::PrefetchRequest(const char *client_name, size_t bytes): Request(client_name) {
  setType(kPrefetch);
  addFrame(&bytes, sizeof(size_t));
}

PrefetchRequest::PrefetchRequest(Request &base_request): Request(base_request) {
  assert(what() == kPrefetch);
}

size_t PrefetchRequest::prefetchSize() {
  return *reinterpret_cast<size_t *>(frames_[2].first);
}

MemInfoRequest::MemInfoRequest(const char *client_name) : Request(client_name) {
  setType(kMemInfo);
}

MemInfoRequest::MemInfoRequest(Request &base_request) : Request(base_request) {
  assert(what() == kMemInfo);
}

MemAllocRequest::MemAllocRequest(const char *client_name, size_t delta_size, bool increase)
    : Request(client_name) {
  setType(kMemAlloc);
  addFrame(&delta_size, sizeof(size_t));
  addFrame(&increase, sizeof(bool));
}

MemAllocRequest::MemAllocRequest(Request &base_request) : Request(base_request) {
  assert(what() == kMemAlloc);
}

size_t MemAllocRequest::deltaSize() { return *reinterpret_cast<size_t *>(frames_[2].first); }

bool MemAllocRequest::isIncrease() { return *reinterpret_cast<bool *>(frames_[3].first); }

HeartbeatRequest::HeartbeatRequest(const char *client_name) : Request(client_name) {
  setType(kHeartbeat);
}

HeartbeatRequest::HeartbeatRequest(Request &base_request) : Request(base_request) {
  assert(what() == kHeartbeat);
}