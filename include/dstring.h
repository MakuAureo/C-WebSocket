#ifndef DSTRING_H
#define DSTRING_H

#include <stddef.h>

typedef struct {
  char * string;
  size_t length;
  size_t capacity;
} DString;

void * dstrinit(DString * str, char const * string, size_t const length);
void dstrfree(DString * str);

int dstrcmp(DString const * const str1, DString const * const str2);
DString * dstrcat(DString * dest, DString const * const src);
DString * dstrcpy(DString * dest, DString const * const src);
DString * dstrdup(DString const * const src);
DString * dstrstr(DString const * const src, char const * const sub);

#endif
