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

#include "response.hpp"

Response::Response() : Message() {}

Response::Response(vector<zmq_msg_t> &zmq_frames) : Message(zmq_frames) {}

Response::~Response() {}

TokenResponse::TokenResponse() : Response() {}

TokenResponse::TokenResponse(double quota) : Response() {
  setType(kToken);
  addFrame(&quota, sizeof(double));
}

TokenResponse::TokenResponse(vector<zmq_msg_t> &zmq_frames) : Response(zmq_frames) {
  assert(what() == kToken);
}

double TokenResponse::quota() { return *reinterpret_cast<double *>(frames_[1].first); }

MemInfoResponse::MemInfoResponse() : Response() {}

MemInfoResponse::MemInfoResponse(size_t used, size_t total) : Response() {
  setType(kMemInfo);
  addFrame(&used, sizeof(size_t));
  addFrame(&total, sizeof(size_t));
}

MemInfoResponse::MemInfoResponse(vector<zmq_msg_t> &zmq_frames) : Response(zmq_frames) {
  assert(what() == kMemInfo);
}

size_t MemInfoResponse::used() { return *reinterpret_cast<size_t *>(frames_[1].first); }

size_t MemInfoResponse::total() { return *reinterpret_cast<size_t *>(frames_[2].first); }

MemAllocResponse::MemAllocResponse() : Response() {}

MemAllocResponse::MemAllocResponse(bool permitted) : Response() {
  setType(kMemAlloc);
  addFrame(&permitted, sizeof(bool));
}

MemAllocResponse::MemAllocResponse(vector<zmq_msg_t> &zmq_frames) : Response(zmq_frames) {
  assert(what() == kMemAlloc);
}

bool MemAllocResponse::permitted() { return *reinterpret_cast<bool *>(frames_[1].first); }

HeartbeatResponse::HeartbeatResponse() : Response() {}

HeartbeatResponse::HeartbeatResponse(vector<zmq_msg_t> &zmq_frames) : Response(zmq_frames) {
  assert(what() == kHeartbeat);
}