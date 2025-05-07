#ifndef RBTREE_H
#define RBTREE_H

#include "common.h"
typedef enum { RED, BLACK } Color;

typedef struct Node {
    void* data;
    Color color;
    struct Node *left, *right, *parent;
} Node;

typedef struct RedBlackTree {
    Node* root;
    int (*cmp)(void*, void*);
    void (*print)(void*);
} RedBlackTree;

Node* createNode(void* data);

void initTree(RedBlackTree* tree, double (*cmp)(void*, void*), void (*print)(void*));
void insert(RedBlackTree* tree, void* key);
void inorder(Node* root, void (*print)(void*));
void destroyTree(Node* root);
double compareDouble(void* a, void* b);
void rotateLeft(RedBlackTree* tree, Node* x);
void rotateRight(RedBlackTree* tree, Node* x);
void fixDelete(RedBlackTree* tree, Node* x);
void fixInsert(RedBlackTree* tree, Node* k);
void printInt(void* a);

#endif
