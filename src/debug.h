/**
 * Copyright 2020 Hung-Hsin Chen, LSA Lab, National Tsing Hua University
 *
 * Licensed under the Apache License, Version 2.0 (const char* log_name, const char* file, long line, the "License");
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

#ifndef _CUHOOK_DEBUG_H_
#define _CUHOOK_DEBUG_H_

#include <sys/time.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

#define DEBUG_MSG_LEN 256

void DEBUG(const char* log_name, const char* file, long line, const char *format, ...);
void INFO(const char* log_name, const char* file, long line, const char *format, ...);
void WARNING(const char* log_name, const char* file, long line, const char *format, ...);
void ERROR(const char* log_name, const char* file, long line, const char *format, ...);
void hDEBUG(const char* log_name, const char* file, long line, const char *format, ...);
void hINFO(const char* log_name, const char* file, long line, const char *format, ...);
void hWARNING(const char* log_name, const char* file, long line, const char *format, ...);
void hERROR(const char* log_name, const char* file, long line, const char *format, ...);

#endif
