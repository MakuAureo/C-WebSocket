#ifndef RADIXTREE_H
#define RADIXTREE_H

#include <stdint.h>
#include <stddef.h>

// Radix tree implementation from: https://db.in.tum.de/~leis/papers/ART.pdf

typedef struct Node Node;
typedef struct Tree RadixTree;

enum NodeType {
  NODE4 = 4,
  NODE16 = 16,
  NODE48 = 48,
  NODE256 = 256
};

struct Node4 {
  uint8_t keys[NODE4];
  Node * child[NODE4];
};

struct Node16 {
  uint8_t keys[NODE16];
  Node * child[NODE16];
};

struct Node48 {
  uint8_t keys[256];
  Node * child[NODE48];
};

struct Node256 {
  Node * child[NODE256];
};

union Node_t {
  struct Node4 node4;
  struct Node16 node16;
  struct Node48 node48;
  struct Node256 node256;
};

struct Node {
  uint8_t count;
  enum NodeType type;
  union Node_t node;
  char * path;
  void * data;
};

struct Tree {
  Node * root;
};

void * initRadixTree(RadixTree * tree);
void freeRadixTree(RadixTree * tree);

int8_t radixTreeInsertRegex(RadixTree * tree, char const * path);
void * radixTreeSearchRegex(RadixTree * tree, char const * path);

#endif
