#ifndef LOG_H
#define LOG_H

#include "printf.h"

// #define NDEBUG

#define WARN      "\001" "1"
#define LOG       "\001" "2"

#define vmm_warn(...) printf(WARN __VA_ARGS__)

#ifdef NDEBUG

#define vmm_log(...)  (void)0

#else   /* !NDEBUG */

#define vmm_log(...)  printf(LOG __VA_ARGS__)

#endif  /* NDEBUG */

#define vmm_warn_on(cond, ...)  \
  do {    \
    if((cond))    \
      vmm_warn(__VA_ARGS__);   \
  } while(0)

#define vmm_bug_on(cond, ...)   \
  do {    \
    if((cond))    \
      panic(__VA_ARGS__);   \
  } while(0)

#define build_bug_on(cond) \
  (void)(sizeof(struct { int:-!!(cond); }))

#endif  /* LOG_H */
