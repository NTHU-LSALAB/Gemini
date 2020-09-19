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

#include "message.hpp"

#include <cstring>
#include <stdexcept>

Message::Message() {
  ServiceType type = kUnknown;
  addFrame(&type, sizeof(ServiceType));
}

Message::Message(vector<zmq_msg_t> &zmq_frames) { deserialize(zmq_frames); }

Message::~Message() { clear(); }

void Message::addFrame(const void *frame_data, size_t frame_size) {
  frames_.push_back(make_pair(nullptr, 0));
  updateFrame(frames_.size() - 1, frame_data, frame_size);
}

void Message::updateFrame(int frame_idx, const void *new_data, size_t frame_size) {
  char *buffer = new char[frame_size];
  memcpy(buffer, new_data, frame_size);
  frames_[frame_idx] = make_pair(buffer, frame_size);
}

void Message::clearFrame(int frame_idx) {
  if (frames_[frame_idx].first != nullptr) {
    delete frames_[frame_idx].first;
  }
  frames_[frame_idx] = make_pair(nullptr, 0);
}

void Message::deserialize(vector<zmq_msg_t> &zmq_frames) {
  clear();
  for (int i = 0; i < zmq_frames.size(); i++) {
    addFrame(zmq_msg_data(&zmq_frames[i]), zmq_msg_size(&zmq_frames[i]));
  }
}

void Message::clear() {
  for (int i = 0; i < frames_.size(); i++) {
    clearFrame(i);
  }
  frames_.clear();
}

ServiceType Message::what() { return *reinterpret_cast<ServiceType *>(frames_[0].first); }

void Message::setType(ServiceType type) { updateFrame(0, &type, sizeof(ServiceType)); }