/**
 * Unified communication interface.
 * This should be the only place for message/request creation.
 */

#include "comm.h"

reqid_t prepare_request(char *buf, comm_request_t type, ...) {
  static char *client_name = nullptr;
  static size_t client_name_len = 0;
  static reqid_t id = 0;
  size_t pos = 0;
  va_list vl;

  if (client_name == nullptr) {
    client_name = getenv("POD_NAME");
    if (client_name == nullptr) {
      client_name = (char *)malloc(HOST_NAME_MAX);
      gethostname(client_name, HOST_NAME_MAX);
    }
    client_name_len = strlen(client_name);
  }

  append_msg_data(buf, pos, client_name_len);
  strncpy(buf + pos, client_name, client_name_len);
  pos += client_name_len;
  append_msg_data(buf, pos, '\0');  // append a terminator
  append_msg_data(buf, pos, id);
  append_msg_data(buf, pos, type);

  // extra information for specific types
  if (type == REQ_QUOTA) {
    va_start(vl, 3);
    append_msg_data(buf, pos, va_arg(vl, double));  // overuse
    append_msg_data(buf, pos, va_arg(vl, double));  // burst duration
    append_msg_data(buf, pos, va_arg(vl, double));  // window period
    va_end(vl);
  } else if (type == REQ_MEM_UPDATE) {
    va_start(vl, 2);
    append_msg_data(buf, pos, va_arg(vl, size_t));  // bytes
    append_msg_data(buf, pos, va_arg(vl, int));     // is_allocate
    va_end(vl);
  }

  return id++;
}

// fill corresponding data into passed arguments
char *parse_request(char *buf, char **name, size_t *name_len, reqid_t *id, comm_request_t *type) {
  size_t pos = 0;
  size_t name_len_;
  char *name_;
  reqid_t id_;
  comm_request_t type_;

  name_len_ = get_msg_data<size_t>(buf, pos);
  name_ = buf + sizeof(size_t);
  pos += name_len_ + 1;  // 1 for the terminator
  id_ = get_msg_data<reqid_t>(buf, pos);
  type_ = get_msg_data<comm_request_t>(buf, pos);

  if (name != nullptr) *name = name_;
  if (name_len != nullptr) *name_len = name_len_;
  if (id != nullptr) *id = id_;
  if (type != nullptr) *type = type_;
  return buf + pos;
}

size_t prepare_response(char *buf, comm_request_t type, reqid_t id, ...) {
  size_t pos = 0;
  va_list vl;

  append_msg_data(buf, pos, id);

  // extra information for specific types
  if (type == REQ_QUOTA) {
    va_start(vl, 1);
    append_msg_data(buf, pos, va_arg(vl, double));  // quota
    va_end(vl);
  } else if (type == REQ_MEM_UPDATE) {
    va_start(vl, 1);
    append_msg_data(buf, pos, va_arg(vl, int));  // verdict
    va_end(vl);
  } else if (type == REQ_MEM_LIMIT) {
    va_start(vl, 2);
    append_msg_data(buf, pos, va_arg(vl, size_t));  // used memory
    append_msg_data(buf, pos, va_arg(vl, size_t));  // total memory
    va_end(vl);
  }

  return pos;
}

char *parse_response(char *buf, reqid_t *id) {
  size_t pos = 0;
  reqid_t id_;

  id_ = get_msg_data<reqid_t>(buf, pos);

  if (id != nullptr) *id = id_;

  return buf + pos;
}
