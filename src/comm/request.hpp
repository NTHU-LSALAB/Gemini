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

#pragma once

#include "message.hpp"

const size_t kMaxClientNameLen = 1024;

// Requests which is often sent by clients.
// Requests starts with sending client's name.
class Request : public Message {
 public:
  // Create a request of unknown service type.
  Request();
  // Create a Request from several zeromq messages.
  Request(vector<zmq_msg_t> &zmq_frames);
  // Create a Request with sending client's name.
  Request(const char *client_name);
  // Create a copy of another Request.
  Request(Request &);
  virtual ~Request();
  // Tell the sender of this Request.
  string from();
};

class KernelTokenRequest : public Request {
 public:
  KernelTokenRequest(const char *client_name, double overuse, double next_burst);
  KernelTokenRequest(Request &request);
  double overuse();
  double nextBurst();
};

class PrefetchTokenRequest : public Request {
 public:
  PrefetchTokenRequest(const char *client_name, double overuse, double next_burst);
  PrefetchTokenRequest(Request &request);
  double overuse();
  double nextBurst();
};

class PrefetchRequest : public Request {
 public:
  PrefetchRequest(const char *client_name, size_t bytes);
  PrefetchRequest(Request &request);
  size_t prefetchSize();
};

class MemInfoRequest : public Request {
 public:
  MemInfoRequest(const char *client_name);
  MemInfoRequest(Request &request);
};

class MemAllocRequest : public Request {
 public:
  // Set increase as true for allocation, and false for freeing.
  MemAllocRequest(const char *client_name, size_t delta_size, bool increase);
  MemAllocRequest(Request &request);
  size_t deltaSize();
  bool isIncrease();
};

class HeartbeatRequest : public Request {
 public:
  HeartbeatRequest(const char *client_name);
  HeartbeatRequest(Request &request);
};