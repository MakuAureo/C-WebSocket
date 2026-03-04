#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
  void * entries;
  uint32_t count;
  uint32_t capacity;
  size_t key_size;
  size_t value_size;
  int8_t (*cmp)(void const * key1, void const  * key2);
  uint32_t (*hashKey)(void * key, size_t length);
} Map;

void initMap(Map * map, size_t key_size, size_t value_size, int8_t (*cmp)(void const * key1, void const  * key2), uint32_t (*hash)(void * key, size_t length));
void freeMap(Map * map);

//Returns 1 if key-value is a new pair or 0 if key already existed and value was updated
int8_t mapPut(Map * map, void * key, void * value);
void mapRemove(Map * map, void * key);
void * mapGet(Map * map, void * key);
void mapClear(Map * map);
void mapForEach(Map * map, void * context, void (*func)(void * key, void * value, void * context));

#endif
