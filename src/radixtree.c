#include "radixtree.h"

#include <stdlib.h>
#include <string.h>
#include <regex.h>

static void freeNode(Node * node) {
  if (node->isLeaf) {
    free(node);
    node = NULL;
    return;
  }
  switch (node->type) {
    case NODE4:
      for (uint32_t i = 0; i < node->count; i++)
        freeNode(node->node.node4.child[i]);
      break;
    case NODE16:
      for (uint32_t i = 0; i < node->count; i++)
        freeNode(node->node.node16.child[i]);
      break;
    case NODE48:
      for (uint32_t i = 0; i < node->count; i++)
        freeNode(node->node.node48.child[i]);
      break;
    case NODE256:
      //This is the only sparse node type, all other nodes store their children sequentially
      for (uint32_t i = 0; i < NODE256; i++)
        if (node->node.node256.child[i] != NULL)
          freeNode(node->node.node256.child[i]);
      break;
  }
  free(node);
  node = NULL;
}

void * initRadixTree(RadixTree * tree) {
  tree->root = calloc(1, sizeof(Node));
  if (tree->root == NULL)
    return NULL;

  tree->root->count = 0;
  tree->root->isLeaf = 1;
  tree->root->type = NODE4;
  tree->root->path = "";
  memset(&(tree->root->node), 0, sizeof(union Node_t));
  return tree->root;
}

void freeRadixTree(RadixTree * tree) {
  freeNode(tree->root);
}

static Node * matchCurrentChar(Node * currNode, char const * curr) {
  switch (currNode->type) {
    // Linear search for small case O(4) -> O(1)
    case NODE4:
      for (uint32_t i = 0; i < currNode->count; i++)
        if (*curr == currNode->node.node4.keys[i])
          return currNode->node.node4.child[i];
      break;

    // Binary search for medium O(log16) -> O(1)
    case NODE16:;
      uint32_t min = 0;
      uint32_t max = currNode->count;
      while (min <= max) {
        uint32_t mid = (min + max) / 2;
        char cmid = currNode->node.node16.keys[mid];
        if (*curr == cmid)
          return currNode->node.node16.child[mid];
        else if (*curr > cmid)
          min = mid + 1;
        else
          max = mid - 1;
      }
      break;

    // Use the current char as the index in the key array (0 <= curr <= 255)
    // key array should be initialized to 255 O(1)
    case NODE48:;
      uint8_t idx;
      if ((idx = currNode->node.node48.keys[*curr]) < 48)
        return currNode->node.node48.child[idx];
      break;

    // Use current char as the index of the child array and check for null O(1)
    case NODE256:
      if (currNode->node.node256.child[*curr] != NULL)
        return currNode->node.node256.child[*curr];
      break;
  }

  return NULL;
}

// /path/to/\d+
int8_t radixTreeInsertRegex(RadixTree * tree, char const * path) {
  regex_t reg;
  int32_t isValidRegex;
  if ((isValidRegex = regcomp(&reg, path, REG_EXTENDED)) != 0)
    return isValidRegex;

  //Find best path then split it
  // '/path/thy' -> '/path/t' + ('o/\d+', 'hy')
  Node * currNode = tree->root;
  for (char const * curr = path; *curr != '\0';) {
    while (*(currNode->path + (curr - path)) != '\0') {
      curr++;
    }
  }

  regfree(&reg);
  return 0;
}

void * radixTreeSearchRegex(RadixTree * tree, char const * path) {
  
}
