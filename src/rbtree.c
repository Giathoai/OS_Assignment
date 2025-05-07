#include <stdio.h>
#include <stdlib.h>
#include "rbtree.h"

Node* createNode(void* data) {
    Node* node = (Node*)malloc(sizeof(Node));
    node->data = data;
    node->color = RED;
    node->left = node->right = node->parent = NULL;
    return node;
}

void rotateLeft(RedBlackTree* tree, Node* x) {
    Node* y = x->right;
    x->right = y->left;
    if (y->left != NULL)
        y->left->parent = x;

    y->parent = x->parent;

    if (x->parent == NULL)
        tree->root = y;
    else if (x == x->parent->left)
        x->parent->left = y;
    else
        x->parent->right = y;

    y->left = x;
    x->parent = y;
}

void rotateRight(RedBlackTree* tree, Node* x) {
    Node* y = x->left;
    x->left = y->right;
    if (y->right != NULL)
        y->right->parent = x;

    y->parent = x->parent;

    if (x->parent == NULL)
        tree->root = y;
    else if (x == x->parent->right)
        x->parent->right = y;
    else
        x->parent->left = y;

    y->right = x;
    x->parent = y;
}

void fixInsert(RedBlackTree* tree, Node* k) {
    Node* u;
    while (k->parent != NULL && k->parent->color == RED) {
        if (k->parent == k->parent->parent->left) {
            u = k->parent->parent->right;
            if (u != NULL && u->color == RED) {
                k->parent->color = BLACK;
                u->color = BLACK;
                k->parent->parent->color = RED;
                k = k->parent->parent;
            } else {
                if (k == k->parent->right) {
                    k = k->parent;
                    rotateLeft(tree, k);
                }
                k->parent->color = BLACK;
                k->parent->parent->color = RED;
                rotateRight(tree, k->parent->parent);
            }
        } else {
            u = k->parent->parent->left;
            if (u != NULL && u->color == RED) {
                k->parent->color = BLACK;
                u->color = BLACK;
                k->parent->parent->color = RED;
                k = k->parent->parent;
            } else {
                if (k == k->parent->left) {
                    k = k->parent;
                    rotateRight(tree, k);
                }
                k->parent->color = BLACK;
                k->parent->parent->color = RED;
                rotateLeft(tree, k->parent->parent);
            }
        }
        if (k == tree->root)
            break;
    }
    tree->root->color = BLACK;
}

void insert(RedBlackTree* tree, void* key) {
    Node* node = createNode(key);
    Node* y = NULL;
    Node* x = tree->root;

    while (x != NULL) {
        y = x;
        if (tree->cmp(node->data, x->data) < 0)
            x = x->left;
        else
            x = x->right;
    }

    node->parent = y;
    if (y == NULL) {
        tree->root = node;
    } else if (tree->cmp(node->data, y->data) < 0) {
        y->left = node;
    } else {
        y->right = node;
    }

    fixInsert(tree, node);
}

void inorder(Node* root, void (*print)(void*)) {
    if (root == NULL)
        return;

    inorder(root->left, print);
    print(root->data);
    printf("(%c) ", root->color == RED ? 'R' : 'B');
    inorder(root->right, print);
}

void initTree(RedBlackTree* tree, int (*cmp)(void*, void*), void (*print)(void*)) {
    tree->root = NULL;
    tree->cmp = cmp;
    tree->print = print;
}

void destroyTree(Node* root) {
    if (root == NULL)
        return;

    destroyTree(root->left);
    destroyTree(root->right);

    free(root);
}

/**
 * Compares two double values.
 * @param a the first double value
 * @param b the second double value
**/
int compareDouble(void* a, void* b) {
    struct pcb_t* proc_a = (struct pcb_t*)a;
    struct pcb_t* proc_b = (struct pcb_t*)b;
    double diff = proc_a->vruntime - proc_b->vruntime;
    if (diff < 0) return -1;
    else if (diff > 0) return 1;
    return 0;
}

void printInt(void* a) {
    printf("%d", *(int*)a);
}

Node* minimum_node(Node* node) {
    while (node->left != NULL) {
        node = node->left;
    }
    return node;
}

Node* replace_node(RedBlackTree* tree, Node* root, Node* target, Node* child) {
    if (target->parent == NULL) {
        tree->root = child;
    } else if (target == target->parent->left) {
        target->parent->left = child;
    } else {
        target->parent->right = child;
    }
    if (child != NULL)
        child->parent = target->parent;

    return tree->root;
}


Node* delete_node(RedBlackTree* tree, Node* root, void* key, int (*cmp)(void*, void*)) {
    Node* z = root;
    while (z != NULL) {
        int comp = cmp(key, z->data);
        if (comp == 0) break;
        else if (comp < 0) z = z->left;
        else z = z->right;
    }

    if (z == NULL) return root;

    Node* y = z;
    Node* x = NULL;
    int y_original_color = y->color;

    if (z->left == NULL) {
        x = z->right;
        root = replace_node(tree, root, z, z->right);
    } else if (z->right == NULL) {
        x = z->left;
        root = replace_node(tree, root, z, z->left);
    } else {
        y = minimum_node(z->right);
        y_original_color = y->color;
        x = y->right;

        if (y->parent == z) {
            if (x != NULL) x->parent = y;
        } else {
            root = replace_node(tree, root, y, y->right);
            y->right = z->right;
            if (y->right != NULL) y->right->parent = y;
        }

        root = replace_node(tree, root, z, y);
        y->left = z->left;
        if (y->left != NULL) y->left->parent = y;
        y->color = z->color;
    }

    z->data = NULL;
    free(z);

    if (y_original_color == BLACK) {
        if (x != NULL)
            fixDelete(tree, x);
        else {
            Node* nil = (Node*)malloc(sizeof(Node));
            nil->color = BLACK;
            nil->left = nil->right = NULL;
            nil->parent = y->parent;
            fixDelete(tree, nil);
            if (nil->parent != NULL) {
                if (nil->parent->left == nil) nil->parent->left = NULL;
                else if (nil->parent->right == nil) nil->parent->right = NULL;
            }
            free(nil);
        }
    }

    return tree->root;
}

void fixDelete(RedBlackTree* tree, Node* x) {
    while (x != tree->root && (x == NULL || x->color == BLACK)) {
        if (x->parent == NULL) break; // Prevent segfault

        if (x == x->parent->left) {
            Node* w = x->parent->right;

            if (w != NULL && w->color == RED) {
                w->color = BLACK;
                x->parent->color = RED;
                rotateLeft(tree, x->parent);
                w = x->parent->right;
            }

            if ((w == NULL) ||
                ((w->left == NULL || w->left->color == BLACK) &&
                 (w->right == NULL || w->right->color == BLACK))) {
                if (w) w->color = RED;
                x = x->parent;
            } else {
                if (w->right == NULL || w->right->color == BLACK) {
                    if (w->left) w->left->color = BLACK;
                    w->color = RED;
                    rotateRight(tree, w);
                    w = x->parent->right;
                }

                w->color = x->parent->color;
                x->parent->color = BLACK;
                if (w->right) w->right->color = BLACK;
                rotateLeft(tree, x->parent);
                x = tree->root;
            }
        } else {
            Node* w = x->parent->left;

            if (w != NULL && w->color == RED) {
                w->color = BLACK;
                x->parent->color = RED;
                rotateRight(tree, x->parent);
                w = x->parent->left;
            }

            if ((w == NULL) ||
                ((w->right == NULL || w->right->color == BLACK) &&
                 (w->left == NULL || w->left->color == BLACK))) {
                if (w) w->color = RED;
                x = x->parent;
            } else {
                if (w->left == NULL || w->left->color == BLACK) {
                    if (w->right) w->right->color = BLACK;
                    w->color = RED;
                    rotateLeft(tree, w);
                    w = x->parent->left;
                }

                w->color = x->parent->color;
                x->parent->color = BLACK;
                if (w->left) w->left->color = BLACK;
                rotateRight(tree, x->parent);
                x = tree->root;
            }
        }
    }

    if (x) x->color = BLACK;
}
