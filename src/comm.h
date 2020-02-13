#ifndef _CUHOOK_COMM_H_
#define _CUHOOK_COMM_H_

#include <unistd.h>

#include <climits>
#include <cstdarg>
#include <cstdlib>
#include <cstring>

typedef int32_t reqid_t;
enum comm_request_t { REQ_QUOTA, REQ_MEM_LIMIT, REQ_MEM_UPDATE };
const size_t REQ_MSG_LEN = 80;
const size_t RSP_MSG_LEN = 40;

reqid_t prepare_request(char *buf, comm_request_t type, ...);

char *parse_request(char *buf, char **name, size_t *name_len, reqid_t *id, comm_request_t *type);

size_t prepare_response(char *buf, comm_request_t type, reqid_t id, ...);

char *parse_response(char *buf, reqid_t *id);

// helper function for parsing message
template <typename T>
T get_msg_data(char *buf, size_t &pos) {
  T data;
  memcpy(&data, buf + pos, sizeof(T));
  pos += sizeof(T);
  return data;
}

// helper function for creating message
template <typename T>
size_t append_msg_data(char *buf, size_t &pos, T data) {
  memcpy(buf + pos, &data, sizeof(T));
  return (pos = pos + sizeof(T));
}

#endif