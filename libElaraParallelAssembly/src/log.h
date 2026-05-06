#include <stdio.h>
#include <stdint.h>

#ifndef EPA_GL_TRACE
#define EPA_GL_TRACE 1
#endif

#if EPA_GL_TRACE
  #define TRACE(...) do { fprintf(stderr, __VA_ARGS__); } while (0)
#else
  #define TRACE(...) do {} while (0)
#endif
