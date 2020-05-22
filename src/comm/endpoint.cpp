#include "endpoint.hpp"

#include <stdexcept>

Endpoint::Endpoint(void *ctx, int type) {
  if (ctx == nullptr) {
    context_ = zmq_ctx_new();
    socket_ = zmq_socket(context_, type);
  } else {
    socket_ = zmq_socket(ctx, type);
  }
}

Endpoint::~Endpoint() {
  zmq_close(socket_);
  if (context_ != nullptr) {
    zmq_ctx_term(context_);
  }
}

int Endpoint::send(Message &msg) {
  int rc = 0, num_frames = msg.frames_.size();
  for (int i = 0; i < num_frames; i++) {
    rc = zmq_send(socket_, msg.frames_[i].first, msg.frames_[i].second,
                  i < num_frames - 1 ? ZMQ_SNDMORE : 0);
    if (rc == -1) {
      throw std::runtime_error("zmq_send failed: " + getZmqErrorMsg());
    }
  }
  return 0;
}

int Endpoint::receive(vector<zmq_msg_t> &frames) {
  int more, rc;
  size_t more_size = sizeof(int);
  do {
    frames.push_back(zmq_msg_t());
    zmq_msg_init(&frames.back());
    rc = zmq_msg_recv(&frames.back(), socket_, 0);
    if (rc == -1) {
      throw std::runtime_error("zmq_msg_recv failed: " + getZmqErrorMsg());
    }
    zmq_getsockopt(socket_, ZMQ_RCVMORE, &more, &more_size);
  } while (more == 1);
  return 0;
}

string Endpoint::getZmqErrorMsg() {
  char err_msg[256];
  snprintf(err_msg, sizeof(err_msg), zmq_strerror(zmq_errno()));
  return string(err_msg);
}

Requester::Requester(void *ctx, const char *url) : Endpoint(ctx, ZMQ_REQ) {
  if (zmq_connect(socket_, url) == -1) {
    throw std::runtime_error("zmq_connect failed: " + getZmqErrorMsg());
  }
  mutex_ = PTHREAD_MUTEX_INITIALIZER;
}

int Requester::submit(Request &request, Response *response_ptr) {
  // REQ socket does not allow another send until previous send receives response.
  pthread_mutex_lock(&mutex_);

  vector<zmq_msg_t> frames;
  send(request);
  receive(frames);

  // another send is allowed now.
  pthread_mutex_unlock(&mutex_);

  if (response_ptr != nullptr) {
    response_ptr->deserialize(frames);
  }

  // data already stored in response_ptr
  for (int i = 0; i < frames.size(); i++) {
    zmq_msg_close(&frames[i]);
  }
  return 0;
}

Responder::Responder(void *ctx, const char *url) : Endpoint(ctx, ZMQ_REP) {
  if (zmq_bind(socket_, url) == -1) {
    throw std::runtime_error("zmq_bind failed: " + getZmqErrorMsg());
  }
}

int Responder::getRequest(Request *request_ptr) {
  vector<zmq_msg_t> frames;
  receive(frames);
  request_ptr->deserialize(frames);

  // data already stored in request_ptr
  for (int i = 0; i < frames.size(); i++) {
    zmq_msg_close(&frames[i]);
  }
  return 0;
}

int Responder::sendResponse(Response &response) {
  send(response);
  return 0;
}