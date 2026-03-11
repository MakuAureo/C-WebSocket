#include "radixtree.h"

#include <stdlib.h>

static void freeNode(Node * node) {
  if (node->isLeaf) {
    free(node);
    return;
  }
  for (uint8_t i = 0; i < node->type; i++) {
    switch (node->type) {
      case NODE4:
        if (node->node.node4.child[i] != NULL)
          freeNode(node->node.node4.child[i]);
        break;
      case NODE16:
        if (node->node.node16.child[i] != NULL)
          freeNode(node->node.node16.child[i]);
        break;
      case NODE48:
        if (node->node.node48.child[i] != NULL)
          freeNode(node->node.node48.child[i]);
        break;
      case NODE256:
        if (node->node.node256.child[i] != NULL)
          freeNode(node->node.node256.child[i]);
        break;
    }
  }
  free(node);
}

void * initRadixTree(RadixTree * tree) {
  tree->root = calloc(1, sizeof(Node));
  if (tree->root == NULL)
    return NULL;

  tree->root->count = 0;
  tree->root->isLeaf = 1;
  tree->root->type = NODE4;
  return tree->root;
}

void freeRadixTree(RadixTree * tree) {
  freeNode(tree->root);
}

// /path/to/\d+
int8_t radixTreeInsertRegex(RadixTree * tree, char const * path) {
  
}

void * radixTreeSearch(RadixTree * tree, char const * path) {

}
