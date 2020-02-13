#include "debug.h"

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
  void func(const char *format, ...) {                           \
    char buf[DEBUG_MSG_LEN], date_buf[100];                      \
    va_list args;                                                \
                                                                 \
    sprint_date(date_buf, 100);                                  \
    va_start(args, format);                                      \
    vsnprintf(buf, DEBUG_MSG_LEN, format, args);                 \
                                                                 \
    fprintf(stderr, "%s Gemini " level "/ %s\n", date_buf, buf); \
  }

#ifdef _DEBUG
GENERATE_PRINT(DEBUG, "D")
#else
void DEBUG(const char *format, ...) {}
#endif
GENERATE_PRINT(INFO, "I")
GENERATE_PRINT(WARNING, "W")
GENERATE_PRINT(ERROR, "E")
