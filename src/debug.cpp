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

#include "debug.h"
#include<iostream>
#include<fstream>
void sprint_date(char *buf, const size_t len) {
  time_t timer;
  struct tm *tm_info;
  struct timespec ts;

  timer = time(nullptr);
  tm_info = localtime(&timer);

  strftime(buf, len, "%F %T", tm_info);

  char ms_buf[10];
  clock_gettime(CLOCK_REALTIME, &ts);
  snprintf(ms_buf, 10, ".%06ld", ts.tv_nsec / 1000);

  strncat(buf, ms_buf, 10);
}

#define GENERATE_PRINT(func, level)                              \
  void func(const char* log_name, const char* file, long line, const char *format, ...) {                           \
    char buf[DEBUG_MSG_LEN], date_buf[100];                      \
    va_list args;                                                \
                                                                 \
    sprint_date(date_buf, 100);                                  \
    va_start(args, format);                                      \
    vsnprintf(buf, DEBUG_MSG_LEN, format, args);                 \
   fprintf(stderr, "%s " level ":%s:%ld %s\n", date_buf, file, line, buf); \
  }

#define GENERATE_f(func, level)                              \
  void func(const char* log_name, const char* file, long line, const char *format, ...) {                           \
    char buf[DEBUG_MSG_LEN], date_buf[100];                      \
    va_list args;                                                \
                                                                 \
    sprint_date(date_buf, 100);                                  \
    va_start(args, format);                                      \
    vsnprintf(buf, DEBUG_MSG_LEN, format, args);                 \
    std::ofstream logger;                                        \
    logger.open ("/kubeshare/log/hook.log", std::ios::out | std::ios::app);\
    logger<<date_buf<<" "<<level<<":"<<file<<":"<<line<<" "<<buf<<std::endl; \
    logger.close(); \
  }

#ifdef _DEBUG
GENERATE_PRINT(DEBUG, "DEBU")
GENERATE_f(hDEBUG, "DEBU")
#else
void DEBUG(const char* log_name, const char* file, long line, const char *format, ...) {}
void hDEBUG(const char* file, long line, const char *format, ...) {}
#endif
GENERATE_PRINT(INFO, "INFO")
GENERATE_PRINT(WARNING, "WARN")
GENERATE_PRINT(ERROR, "ERRO")
GENERATE_f(hINFO, "INFO")
GENERATE_f(hWARNING, "WARN")
GENERATE_f(hERROR, "ERRO")
//fprintf(stderr, "%s " level "%s:%ld %s\n", date_buf, file, line, buf); 