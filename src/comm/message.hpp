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

#include <zmq.h>

#include <cassert>
#include <string>
#include <vector>

using std::make_pair;
using std::pair;
using std::string;
using std::vector;

enum ServiceType { kKernelToken, kPrefetchToken, kMemInfo, kMemAlloc, kHeartbeat, kUnknown };

class Endpoint;

// Messages of some type, consisting of several frames/buffers.
// Messages can be transmit among Endpoints.
class Message {
  friend class Endpoint;  // give access to frames_.

 public:
  // Initialize a message with Unknown service type.
  Message();
  // Create a message from several zeromq messages.
  Message(vector<zmq_msg_t> &zmq_frames);
  virtual ~Message();
  void addFrame(const void *frame_data, size_t frame_size);
  // Change the content of a frame.
  // Note that a new buffer is allocated internally for each frame.
  void updateFrame(int frame_idx, const void *new_data, size_t frame_size);
  // Clear the data of a frame.
  // Note that content of the cleared frame is replaced by a nullptr.
  void clearFrame(int frame_idx);
  // Reconstruct all frame data from several zeromq messages.
  void deserialize(vector<zmq_msg_t> &zmq_frames);
  // Clear all frames.
  // This will result in a Message with no content, including service type info.
  void clear();
  // Tell the Service Type of this Message.
  // Service Type is the first frame of message.
  ServiceType what();

 protected:
  vector<pair<char *, size_t>> frames_;
  void setType(ServiceType);
};