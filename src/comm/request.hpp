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

class TokenRequest : public Request {
 public:
  TokenRequest(const char *client_name, double overuse, double next_burst);
  TokenRequest(Request &request);
  double overuse();
  double nextBurst();
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