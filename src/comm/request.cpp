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

TokenRequest::TokenRequest(const char *client_name, double overuse, double next_burst)
    : Request(client_name) {
  setType(kToken);
  addFrame(&overuse, sizeof(double));
  addFrame(&next_burst, sizeof(double));
}

TokenRequest::TokenRequest(Request &base_request) : Request(base_request) {
  assert(what() == kToken);
}

double TokenRequest::overuse() { return *reinterpret_cast<double *>(frames_[2].first); }

double TokenRequest::nextBurst() { return *reinterpret_cast<double *>(frames_[3].first); }

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