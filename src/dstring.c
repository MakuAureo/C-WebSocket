#include <dstring.h>

#include <string.h>
#include <stdlib.h>

static int resize(DString * str, int size) {
  size_t newCap = str->capacity;
  while (newCap < size)
    newCap = newCap << 1;

  char * newPtr = realloc(str->string, newCap * sizeof(char));
  if (newPtr == NULL)
    return -1;

  str->capacity = newCap;
  str->string = newPtr;

  return 0;
}

void dstrinit(DString * str, char const * string, size_t const length) {
  str->capacity = 1;
  while (str->capacity < length) 
    str->capacity = str->capacity << 1;
  str->length = length;
  str->string = malloc(str->capacity * sizeof(char));
  memcpy(str->string, string, length);
}

void sdtrfree(DString * str) {
  str->capacity = 0;
  str->length = 0;
  free(str->string);
  str = NULL;
}

int dstrcmp(DString const * const str1, DString const * const str2) {
  if (str1->length != str2->length)
    return 0;

  for (size_t i = 0; i < str1->length; i++)
    if (str1->string[i] != str2->string[i])
      return 0;

  return 1;
}

DString * dstrcat(DString * dest, DString const * const src) {
  if (dest->length + src->length > dest->capacity)
    if (resize(dest, dest->length + src->length) == -1)
      return NULL;

  
}

DString * dstrcpy(DString * dest, DString const * const src);
DString * dstrdup(DString const * const src);
DString * dstrstr(DString const * const src, char const * const sub);

size_t dstrget(DString const * const str, size_t const at);
void dstrset(DString * const str, size_t const start, char const * const );
