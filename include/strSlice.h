#ifndef STRSLICE_H
#define STRSLICE_H

#include <stddef.h>

typedef struct Slice Slice;

struct Slice {
  char const * ptr;
  size_t length;
};

#endif
