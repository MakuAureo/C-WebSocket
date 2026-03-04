#include "hashmap.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HM_TRUE 1
#define HM_FALSE 0
#define HM_MAX_LOAD 0.6
#define HM_MAP_GROWTH 32

static uint32_t defaultHash(void * key, size_t length) {
  uint8_t * bytes = (uint8_t *)key;
  uint32_t hash = 2166136261u;

  for (size_t i = 0; i < length; i++) {
    hash ^= bytes[i];
    hash *= 16777619;
  }

  return hash;
}

static int8_t isNull(uint8_t *bytes, size_t size) {
  for (size_t i = 0; i < size; i++) {
    if (bytes[i] != 0) return HM_FALSE;
  }
  return HM_TRUE;
}

static uint8_t * linearProbing(Map * map, void * entries, void * key, size_t entrySize, size_t capacity) {
  uint32_t hash = map->hashKey(key, map->key_size);
  uint32_t index = hash % capacity;
  uint8_t * tombstone = NULL;

  for (;;) {
    uint8_t * entry = entries + (index * entrySize);
    void * value = entry + map->key_size;

    if (isNull(entry, map->key_size)) {
      if (isNull(value, map->value_size)) {
        return tombstone != NULL ? tombstone : entry;
      } else {
        if (tombstone == NULL) {
          tombstone = entry;
        }
      }
    } else if (map->cmp(entry, key)) {
      return entry;
    }

    index = (index + 1) % map->capacity;
  }
}

static int8_t resizeArray(Map * map) {
  uint32_t newCapacity = map->capacity + HM_MAP_GROWTH;
  if (newCapacity <= 0) return HM_FALSE;
  size_t entrySize = map->key_size + map->value_size;

  uint8_t * oldEntries = (uint8_t *)map->entries;
  uint8_t * newEntries = malloc(newCapacity * entrySize);
  if (newEntries == NULL) return HM_FALSE;

  memset(newEntries, 0, newCapacity * entrySize);
  map->count = 0;

  for (uint32_t i = 0; i < map->capacity; i++) {
    uint8_t * oldEntry = oldEntries + (i * entrySize);
    void * key = oldEntry;
    void * value = oldEntry + map->key_size;

    if (isNull(key, map->key_size)) continue;

    uint8_t * dest = linearProbing(map, newEntries, key, entrySize, newCapacity);

    memcpy(dest, key, map->key_size);
    memcpy(dest + map->key_size, value, map->value_size);
    map->count++;
  }

  free(map->entries);
  map->entries = newEntries;
  map->capacity = newCapacity;
  return HM_TRUE;
}

void initMap(Map * map, size_t key_size, size_t value_size, int8_t (*cmp)(void const * key1, void const  * key2), uint32_t (*hash)(void * key, size_t length)) {
  map->count = 0;
  map->capacity = 0;
  map->entries = NULL;
  map->key_size = key_size;
  map->value_size = value_size;
  map->cmp = cmp;
  map->hashKey = (hash == NULL) ? defaultHash : hash;
}

void freeMap(Map * map) {
  free(map->entries);
  map->count = 0;
  map->capacity = 0;
  map->entries = NULL;
  map->key_size = 0;
  map->value_size = 0;
}

int8_t mapPut(Map * map, void * key, void * value) {
  if (map->count >= map->capacity * HM_MAX_LOAD)
    resizeArray(map);

  uint8_t * entry = linearProbing(map, map->entries, key, (map->key_size + map->value_size), map->capacity);

  int8_t isNewKey = isNull(entry, map->key_size);
  if (isNewKey && isNull(entry + map->key_size, map->value_size))
    map->count++;

  memcpy(entry, key, map->key_size);
  memcpy(entry + map->key_size, value, map->value_size);

  return isNewKey;
}

void mapRemove(Map * map, void * key) {
  if (map->count == 0) return;

  uint8_t * entry = linearProbing(map, map->entries, key, (map->key_size + map->value_size), map->capacity);

  memset(entry, 0, map->key_size);

  // TOMBSTONE : (0000...000001)
  // BITS REP :   ^key...value^
  memset(entry + map->key_size, 0, map->value_size);
  memset(entry + map->key_size + map->value_size - 1, 1, 1);
}

void * mapGet(Map * map, void * key) {
  if (map->capacity == 0) return NULL;

  uint8_t * entry = linearProbing(map, map->entries, key, (map->key_size + map->value_size), map->capacity);

  if (isNull(entry, map->key_size)) 
    return NULL;

  return entry + map->key_size;
}

void mapClear(Map * map) {
  size_t entrySize = map->key_size + map->value_size;
  memset(map->entries, 0, map->capacity * entrySize);
  map->count = 0;
}

void mapForEach(Map * map, void * context, void (*func)(void * key, void * value, void * context)) {
  if (map->count != 0) {
    for (uint32_t i = 0; i < map->capacity; i++) {
      uint8_t * entry = map->entries + (i * (map->key_size + map->value_size));
      void * key = entry;
      void * value = entry + map->key_size;
      uint8_t isEmpty = 1;
      for (size_t j = 0; j < map->key_size; j++) {
        if (entry[j] != 0) {
          isEmpty = 0;
          break;
        }
      }
      if (isEmpty == 1) continue;

      func(key, value, context);
    }
  }
}
