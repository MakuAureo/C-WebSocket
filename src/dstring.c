#include "dstring.h"

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

void * dstrinit(DString * str, char const * string, size_t const length) {
  str->capacity = 1;
  while (str->capacity < length) 
    str->capacity = str->capacity << 1;

  str->length = length;
  if ((str->string = malloc(str->capacity * sizeof(char))) == NULL)
    return NULL;

  memcpy(str->string, string, length);
  str->string[str->capacity - 1] = '\0'; //Fail-safe in case string wasn't null terminated

  return str;
}

void dstrfree(DString * str) {
  str->capacity = 0;
  str->length = 0;
  free(str->string);
  str = NULL;
}

int dstrcmp(DString const * const str1, DString const * const str2) {
  if (str1->length != str2->length)
    return 0;

  size_t const length = str1->length;
  for (size_t i = 0; i < length; i++)
    if (str1->string[i] != str2->string[i])
      return 0;

  return 1;
}

DString * dstrcat(DString * dest, DString const * const src) {
  size_t newSize = dest->length + src->length - 1; //-1 because of the duplicate '\0'
  if (dest->capacity < newSize)
    if (resize(dest, newSize) == -1)
      return NULL;

  memcpy(dest->string + dest->length - 1, src->string, src->length);
  return dest;
}

DString * dstrcpy(DString * dest, DString const * const src) {
  if (dest->capacity < src->length)
    if (resize(dest, src->length) == -1)
      return NULL;

  memcpy(dest->string, src->string, src->length);
  return dest;
}

//The dupped DString is heap-allocated and needs to be manually free'd
DString * dstrdup(DString const * const src) {
  DString * newStr = malloc(sizeof(DString));
  dstrinit(newStr, src->string, src->length);

  return newStr;
}
