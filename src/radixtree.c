#include "radixtree.h"

#include <stdlib.h>
#include <string.h>
#include <regex.h>

static void freeNode(Node * node) {
  if (node->count == 0) {
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
  tree->root->type = NODE4;
  tree->root->path.ptr = "";
  tree->root->path.length = 0;
  tree->root->data = NULL;
  return tree->root;
}

void freeRadixTree(RadixTree * tree) {
  freeNode(tree->root);
}

static Node * findNextNode(Node * currNode, char const * curr) {
  if (currNode->count == 0)
    return NULL;

  uint8_t const uCurr = *(uint8_t *)curr;
  switch (currNode->type) {
    // Linear search for small case O(4) -> O(1)
    case NODE4:
      for (uint8_t i = 0; i < currNode->count; i++)
        if (uCurr == currNode->node.node4.keys[i])
          return currNode->node.node4.child[i];
      break;

    // Binary search for medium O(log16) -> O(4) -> O(1)
    case NODE16:;
      uint8_t min = 0;
      uint8_t max = currNode->count;
      while (min <= max) {
        uint32_t mid = (min + max) / 2;
        uint8_t cmid = currNode->node.node16.keys[mid];
        if (uCurr == cmid)
          return currNode->node.node16.child[mid];
        else if (uCurr > cmid)
          min = mid + 1;
        else
          max = mid - 1;
      }
      break;

    // Use the current char as the index in the key array (0 <= curr <= 255)
    // key array should be initialized to 255 O(1)
    case NODE48:;
      uint8_t idx;
      if ((idx = currNode->node.node48.keys[uCurr]) < 48)
        return currNode->node.node48.child[idx];
      break;

    // Use current char as the index of the child array and check for null O(1)
    case NODE256:
      if (currNode->node.node256.child[uCurr] != NULL)
        return currNode->node.node256.child[uCurr];
      break;
  }

  return NULL;
}

static void upgradeNode(Node * node) {
  union Node_t new;
  memset(&new, 0, sizeof(union Node_t));
  switch (node->type) {
    case NODE4:;
      for (uint8_t i = 0; i < NODE4; i++) {
        new.node16.keys[i] = node->node.node4.keys[i];
        new.node16.child[i] = node->node.node4.child[i];
      }
      memcpy(&(node->node), &new, sizeof(struct Node16));
      node->type = NODE16;
      break;

    case NODE16:
      for (uint8_t * ptr = new.node48.keys; ptr < new.node48.keys + 256; ptr++) {
        *ptr = (uint8_t)255;
      }
      for (uint8_t i = 0; i < NODE16; i++) {
        new.node48.keys[node->node.node16.keys[i]] = i;
        new.node48.child[i] = node->node.node16.child[i];
      }
      memcpy(&(node->node), &new, sizeof(struct Node48));
      node->type = NODE48;
      break;

    case NODE48:
      for (uint8_t i = 0; i < NODE48; i++) {
        new.node256.child[(uint8_t)(node->node.node48.child[i]->path.ptr[0])] = node->node.node48.child[i];
      }
      memcpy(&(node->node), &new, sizeof(struct Node256));
      node->type = NODE256;
      break;

    case NODE256:
      //This shouldn't happen
      break;
  }
}

static int8_t addChild(Node * parent, Node * child) {
  if (parent->count == parent->type)
    upgradeNode(parent);

  char moveChar;
  Node * moveNode;
  switch (parent->type) {
    case NODE4:
      moveChar = child->path.ptr[0];
      moveNode = child;
      for (uint8_t i = 0; i < parent->count; i++) {
        if (parent->node.node4.keys[i] < child->path.ptr[0]) continue;
        else if (parent->node.node4.keys[i] == child->path.ptr[0]) return -1;
        else {
          char temp = parent->node.node4.keys[i];
          parent->node.node4.keys[i] = moveChar;
          moveChar = temp;

          Node * tempNode = parent->node.node4.child[i];
          parent->node.node4.child[i] = moveNode;
          moveNode = tempNode;
        }
      }
      parent->node.node4.keys[parent->count] = moveChar;
      parent->node.node4.child[parent->count] = moveNode;
      break;

    case NODE16:
      moveChar = child->path.ptr[0];
      moveNode = child;
      for (uint8_t i = 0; i < parent->count; i++) {
        if (parent->node.node4.keys[i] < child->path.ptr[0]) continue;
        else if (parent->node.node4.keys[i] == child->path.ptr[0]) return -1;
        else {
          char temp = parent->node.node4.keys[i];
          parent->node.node4.keys[i] = moveChar;
          moveChar = temp;

          Node * tempNode = parent->node.node4.child[i];
          parent->node.node4.child[i] = moveNode;
          moveNode = tempNode;
        }
      }
      parent->node.node4.keys[parent->count] = moveChar;
      parent->node.node4.child[parent->count] = moveNode;
      break;

    case NODE48:
      if (parent->node.node48.keys[(uint8_t)(child->path.ptr[0])] < 48) return -1;
      parent->node.node48.keys[(uint8_t)(child->path.ptr[0])] = parent->count;
      parent->node.node48.child[parent->count] = child;
      break;

    case NODE256:
      if (parent->node.node256.child[(uint8_t)(child->path.ptr[0])] != NULL) return -1;
      parent->node.node256.child[(uint8_t)(child->path.ptr[0])] = child;
      break;
  }

  parent->count++;
  return 0;
}

// /path/to/\d+
int8_t radixTreeInsertRegex(RadixTree * tree, char const * path, void const * data) {
  regex_t reg;
  int32_t isValidRegex;
  if ((isValidRegex = regcomp(&reg, path, REG_EXTENDED)) != 0)
    return isValidRegex;

  //Find best path then split it
  // '/path/thy' -> '/path/t' + ('o/\d+', 'hy')
  char const * currStart = path;
  Node * currNode = tree->root;
  for (char const * curr = currStart;;) {
    enum PATHFLAGS {
      NONE = 0,
      INPUT_PATH_END = 1,
      NODE_PATH_END = 1 << 1,
      NODE_PATH_MATCH = 1 << 2
    };
    enum PATHFLAGS flag;

    do {
      size_t off = curr - currStart;

      flag = NONE;
      if (*curr == '\0') flag |= INPUT_PATH_END;
      if (currNode->path.length <= off) flag |= NODE_PATH_END;
      else if (currNode->path.ptr[off] == *(curr++)) flag |= NODE_PATH_MATCH;
    } while (flag == NODE_PATH_MATCH);
    
    // The input path matches an already existing path
    if (flag == (INPUT_PATH_END | NODE_PATH_END)) {
      regfree(&reg);
      return 0;
    }
    
    // Find the next node that continues the path
    if (flag == NODE_PATH_END) {
      Node * next = findNextNode(currNode, curr);
      if (next == NULL) {
        Node * new = calloc(1, sizeof(Node));

        new->count = 0;
        new->type = NODE4;
        new->data = data;
        new->path.length = strlen(curr);
        new->path.ptr = curr;
        
        addChild(currNode, new);

        regfree(&reg);
        return 0;
      }

      currStart = curr;
      currNode = next;
    }

    // Subpath should also be valid
    else if (flag == INPUT_PATH_END) {
      //TODO: Branch the rest of the node path
    }
  }
}

void * radixTreeSearchRegex(RadixTree * tree, char const * path) {
  return NULL;
}
