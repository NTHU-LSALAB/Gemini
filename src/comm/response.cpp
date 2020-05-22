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