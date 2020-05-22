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

class TokenResponse : public Response {
 public:
  TokenResponse();
  TokenResponse(double quota);
  TokenResponse(vector<zmq_msg_t> &zmq_frames);
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