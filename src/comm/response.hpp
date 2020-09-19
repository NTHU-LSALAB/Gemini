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

// Response of a Request.
class Response : public Message {
 public:
  // Create a Response of unknown service type.
  Response();
  // Create a Response from several zeromq messages.
  Response(vector<zmq_msg_t> &zmq_frames);
  virtual ~Response();
};

class PrefetchTokenResponse : public Response {
 public:
  PrefetchTokenResponse();
  PrefetchTokenResponse(vector<zmq_msg_t> &zmq_frames);
};

class KernelTokenResponse : public Response {
 public:
  KernelTokenResponse();
  KernelTokenResponse(double quota);
  KernelTokenResponse(vector<zmq_msg_t> &zmq_frames);
  double quota();
};

class MemInfoResponse : public Response {
 public:
  MemInfoResponse();
  MemInfoResponse(size_t used, size_t total);
  MemInfoResponse(vector<zmq_msg_t> &zmq_frames);
  size_t used();
  size_t total();
};

class MemAllocResponse : public Response {
 public:
  MemAllocResponse();
  MemAllocResponse(bool permitted);
  MemAllocResponse(vector<zmq_msg_t> &zmq_frames);
  bool permitted();
};

class HeartbeatResponse : public Response {
 public:
  HeartbeatResponse();
  HeartbeatResponse(vector<zmq_msg_t> &zmq_frames);
};