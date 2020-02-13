#ifndef _CUHOOK_DEBUG_H_
#define _CUHOOK_DEBUG_H_

#include <sys/time.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

#define DEBUG_MSG_LEN 256

void DEBUG(const char *format, ...);
void INFO(const char *format, ...);
void WARNING(const char *format, ...);
void ERROR(const char *format, ...);

#endif
