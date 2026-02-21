#ifndef HASHMAP_H
#define HASHMAP_H

#include <stddef.h>

typedef struct {
  void * entries;
  int count;
  int capacity;
  size_t key_size;
  size_t value_size;
  int (*cmp)(void const * key1, void const  * key2);
} Map;

void initMap(Map * map, size_t key_size, size_t value_size, int (*cmp)(void const * key1, void const  * key2));
void freeMap(Map * map);

//Returns 1 if key-value is a new pair or 0 if key already existed and value was updated
int mapPut(Map * map, void * key, void * value);
void mapRemove(Map * map, void * key);
void * mapGet(Map * map, void * key);
void mapClear(Map * map);
void mapForEach(Map * map, void * context, void (*func)(void * value, void * context));

#endif
