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

#include <pthread.h>

#include "request.hpp"
#include "response.hpp"

// A zeromq endpoint.
class Endpoint {
 public:
  // Create a zeromq socket.
  // A zeromq context is also created when ctx is null.
  Endpoint(void *ctx, int type);
  // Close zeromq socket. Also destroy context if created in class constructor.
  ~Endpoint();

 protected:
  void *context_;  // zeromq context
  void *socket_;   // zeromq socket
  // Send frames data in msg. Throw std::runtime_error if zmq_send failed.
  int send(Message &msg);
  // Receive frames from socket and store them in frames.
  // Throw std::runtime_error if zmq_msg_recv failed.
  int receive(vector<zmq_msg_t> &frames);
  // helper function to get zeromq error message from zmq_errno()
  string getZmqErrorMsg();
};

// An zeromq endpoint with a REQ socket.
// Send requests to other endpoints.
class Requester : public Endpoint {
 public:
  // Connect to url. Throw std::runtime_error if zmq_connect failed.
  Requester(void *ctx, const char *url);
  // Send a Request and store its Response into response_ptr.
  // response_ptr often points to a derived class of Response.
  // If response_ptr is null, then response from peer is dropped.
  int submit(Request &request, Response *response_ptr);

 protected:
  pthread_mutex_t mutex_;
};

// An zeromq endpoint with a REP socket.
// Respond to incoming requests from other endpoints.
class Responder : public Endpoint {
 public:
  // Bind socket to url. Throw std::runtime_error if zmq_bind failed.
  Responder(void *ctx, const char *url);
  // Get an incoming request. request_ptr cannot be null.
  int getRequest(Request *);
  int sendResponse(Response &);
};