#include "queue.h"
#include "sched.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

/* CFS scheduler implementation */
#define CFS_SCHED

#ifdef CFS_SCHED
#define DEFAULT_WEIGHT 1024 // Default process weight
#define MIN_VRUNTIME 0		// Minimum virtual runtime
#define MAX_PRIO 100		// Maximum priority value
#endif

static struct queue_t running_list;
static pthread_mutex_t queue_lock;

#ifdef CFS_SCHED
/* Red-Black Tree node for CFS */
struct rb_node
{
	struct pcb_t *proc;		// Process control block
	unsigned long vruntime; // Virtual runtime
	struct rb_node *left;	// Left child
	struct rb_node *right;	// Right child
	struct rb_node *parent; // Parent node
	int color;				// 0 for black, 1 for red
};

static struct rb_node *root = NULL;		// Root of the RB tree
static struct rb_node *leftmost = NULL; // Leftmost node (min vruntime)

/* Helper functions for Red-Black Tree operations */
static struct rb_node *rb_new_node(struct pcb_t *proc)
{
	struct rb_node *node = (struct rb_node *)malloc(sizeof(struct rb_node));
	if (!node)
	{
		fprintf(stderr, "Failed to allocate RB node\n");
		return NULL;
	}
	node->proc = proc;
	// Validate priority and calculate initial vruntime
	unsigned long weight = (proc->prio > 0 && proc->prio <= MAX_PRIO)
							   ? DEFAULT_WEIGHT / proc->prio
							   : DEFAULT_WEIGHT;
	node->vruntime = MIN_VRUNTIME + weight;
	node->left = NULL;
	node->right = NULL;
	node->parent = NULL;
	node->color = 1; // New nodes are red
	return node;
}

/* Left rotation to balance the Red-Black Tree */
static void rb_left_rotate(struct rb_node **root, struct rb_node *x)
{
	struct rb_node *y = x->right;
	x->right = y->left;
	if (y->left)
		y->left->parent = x;
	y->parent = x->parent;
	if (!x->parent)
		*root = y;
	else if (x == x->parent->left)
		x->parent->left = y;
	else
		x->parent->right = y;
	y->left = x;
	x->parent = y;
}

/* Right rotation to balance the Red-Black Tree */
static void rb_right_rotate(struct rb_node **root, struct rb_node *y)
{
	struct rb_node *x = y->left;
	y->left = x->right;
	if (x->right)
		x->right->parent = y;
	x->parent = y->parent;
	if (!y->parent)
		*root = x;
	else if (y == y->parent->left)
		y->parent->left = x;
	else
		y->parent->right = x;
	x->right = y;
	y->parent = x;
}

/* Fix Red-Black Tree properties after insertion */
static void rb_insert_fixup(struct rb_node **root, struct rb_node *node)
{
	struct rb_node *parent, *grandparent, *uncle;
	while (node != *root && node->parent && node->parent->color == 1)
	{
		parent = node->parent;
		grandparent = parent->parent;
		if (!grandparent)
			break;

		if (parent == grandparent->left)
		{
			uncle = grandparent->right;
			// Case 1: Uncle is red
			if (uncle && uncle->color == 1)
			{
				parent->color = 0;
				uncle->color = 0;
				grandparent->color = 1;
				node = grandparent;
			}
			else
			{
				// Case 2: Uncle is black, node is right child
				if (node == parent->right)
				{
					node = parent;
					rb_left_rotate(root, node);
					parent = node->parent;
				}
				// Case 3: Uncle is black, node is left child
				parent->color = 0;
				grandparent->color = 1;
				rb_right_rotate(root, grandparent);
			}
		}
		else
		{
			uncle = grandparent->left;
			// Case 1: Uncle is red
			if (uncle && uncle->color == 1)
			{
				parent->color = 0;
				uncle->color = 0;
				grandparent->color = 1;
				node = grandparent;
			}
			else
			{
				// Case 2: Uncle is black, node is left child
				if (node == parent->left)
				{
					node = parent;
					rb_right_rotate(root, node);
					parent = node->parent;
				}
				// Case 3: Uncle is black, node is right child
				parent->color = 0;
				grandparent->color = 1;
				rb_left_rotate(root, grandparent);
			}
		}
	}
	(*root)->color = 0; // Root is always black
}

/* Insert a node into the Red-Black Tree, sorting by vruntime */
static void rb_insert(struct rb_node **root, struct rb_node *node)
{
	struct rb_node *y = NULL;
	struct rb_node *x = *root;

	// Find insertion position
	while (x != NULL)
	{
		y = x;
		if (node->vruntime < x->vruntime)
			x = x->left;
		else
			x = x->right;
	}

	// Set parent
	node->parent = y;

	// Insert node
	if (y == NULL)
		*root = node; // Tree was empty
	else if (node->vruntime < y->vruntime)
		y->left = node;
	else
		y->right = node;

	// Fix Red-Black properties
	rb_insert_fixup(root, node);

	// Update leftmost node
	if (leftmost == NULL || node->vruntime < leftmost->vruntime)
		leftmost = node;
}

/* Find the node with minimum vruntime (leftmost node) */
static struct rb_node *rb_find_min(struct rb_node *node)
{
	if (!node)
		return NULL;
	while (node->left)
		node = node->left;
	return node;
}

/* Fix Red-Black Tree properties after deletion */
static void rb_remove_fixup(struct rb_node **root, struct rb_node *node, struct rb_node *parent)
{
	struct rb_node *sibling;
	while (node != *root && (!node || node->color == 0))
	{
		if (node == parent->left)
		{
			sibling = parent->right;
			if (sibling && sibling->color == 1)
			{
				sibling->color = 0;
				parent->color = 1;
				rb_left_rotate(root, parent);
				sibling = parent->right;
			}
			if (sibling && (!sibling->left || sibling->left->color == 0) &&
				(!sibling->right || sibling->right->color == 0))
			{
				sibling->color = 1;
				node = parent;
				parent = node->parent;
			}
			else
			{
				if (sibling && (!sibling->right || sibling->right->color == 0))
				{
					if (sibling->left)
						sibling->left->color = 0;
					sibling->color = 1;
					rb_right_rotate(root, sibling);
					sibling = parent->right;
				}
				if (sibling)
				{
					sibling->color = parent->color;
					parent->color = 0;
					if (sibling->right)
						sibling->right->color = 0;
					rb_left_rotate(root, parent);
				}
				node = *root;
			}
		}
		else
		{
			sibling = parent->left;
			if (sibling && sibling->color == 1)
			{
				sibling->color = 0;
				parent->color = 1;
				rb_right_rotate(root, parent);
				sibling = parent->left;
			}
			if (sibling && (!sibling->right || sibling->right->color == 0) &&
				(!sibling->left || sibling->left->color == 0))
			{
				sibling->color = 1;
				node = parent;
				parent = node->parent;
			}
			else
			{
				if (sibling && (!sibling->left || sibling->left->color == 0))
				{
					if (sibling->right)
						sibling->right->color = 0;
					sibling->color = 1;
					rb_left_rotate(root, sibling);
					sibling = parent->left;
				}
				if (sibling)
				{
					sibling->color = parent->color;
					parent->color = 0;
					if (sibling->left)
						sibling->left->color = 0;
					rb_right_rotate(root, parent);
				}
				node = *root;
			}
		}
	}
	if (node)
		node->color = 0;
}

/* Remove a node from the Red-Black Tree */
static void rb_remove(struct rb_node **root, struct rb_node *node)
{
	struct rb_node *child, *parent;
	int color;

	// Find successor if both children exist
	if (node->left && node->right)
	{
		struct rb_node *successor = rb_find_min(node->right);
		node->proc = successor->proc;
		node->vruntime = successor->vruntime;
		node = successor;
	}

	// Node has at most one child
	child = node->left ? node->left : node->right;
	parent = node->parent;
	color = node->color;

	if (child)
		child->parent = parent;

	if (parent)
	{
		if (node == parent->left)
			parent->left = child;
		else
			parent->right = child;
	}
	else
	{
		*root = child;
	}

	// Update leftmost if needed
	if (node == leftmost)
		leftmost = rb_find_min(*root);

	// Fix Red-Black properties if deleting a black node
	if (color == 0)
		rb_remove_fixup(root, child, parent);

	free(node);
}

/* Update vruntime based on execution time and process priority */
static void update_vruntime(struct rb_node *node, int execution_time)
{
	if (!node || execution_time < 0)
		return;
	unsigned long weight = (node->proc->prio > 0 && node->proc->prio <= MAX_PRIO)
							   ? DEFAULT_WEIGHT / node->proc->prio
							   : DEFAULT_WEIGHT;
	node->vruntime += execution_time * weight;
}

/* Free all nodes in the Red-Black Tree */
static void free_rb_tree(struct rb_node *node)
{
	if (!node)
		return;
	free_rb_tree(node->left);
	free_rb_tree(node->right);
	free(node);
}
#endif

/* Check if the scheduler has no processes */
int queue_empty(void)
{
#ifdef CFS_SCHED
	return (root == NULL);
#else
	return (running_list.size == 0);
#endif
}

/* Initialize the scheduler */
void init_scheduler(void)
{
#ifdef CFS_SCHED
	root = NULL;
	leftmost = NULL;
#else
	running_list.size = 0;
#endif
	pthread_mutex_init(&queue_lock, NULL);
	running_list.size = 0;
}

/* Get the process with the minimum vruntime */
struct pcb_t *get_proc(void)
{
#ifdef CFS_SCHED
	pthread_mutex_lock(&queue_lock);
	if (!root)
	{
		pthread_mutex_unlock(&queue_lock);
		return NULL;
	}

	struct rb_node *node = leftmost;
	struct pcb_t *proc = node ? node->proc : NULL;
	if (node)
		rb_remove(&root, node);

	pthread_mutex_unlock(&queue_lock);
	return proc;
#else
	pthread_mutex_lock(&queue_lock);
	struct pcb_t *proc = dequeue(&running_list);
	pthread_mutex_unlock(&queue_lock);
	return proc;
#endif
}

/* Put a process back into the scheduler after execution */
void put_proc(struct pcb_t *proc, int execution_time)
{
	if (!proc || execution_time < 0)
	{
		fprintf(stderr, "Invalid process or execution time in put_proc\n");
		return;
	}

	proc->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	enqueue(&running_list, proc);

#ifdef CFS_SCHED
	// Create a new node for the process
	struct rb_node *node = rb_new_node(proc);
	if (node)
	{
		// Update vruntime based on actual execution time
		update_vruntime(node, execution_time);
		// Insert into Red-Black Tree
		rb_insert(&root, node);
	}
	else
	{
		fprintf(stderr, "Failed to create RB node in put_proc\n");
	}
#else
	enqueue(&running_list, proc);
#endif

	pthread_mutex_unlock(&queue_lock);
}

/* Add a new process to the scheduler */
void add_proc(struct pcb_t *proc)
{
	if (!proc)
	{
		fprintf(stderr, "Invalid process in add_proc\n");
		return;
	}

	proc->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	enqueue(&running_list, proc);

#ifdef CFS_SCHED
	// Create a new node for the process
	struct rb_node *node = rb_new_node(proc);
	if (node)
		rb_insert(&root, node);
	else
		fprintf(stderr, "Failed to create RB node in add_proc\n");
#else
	enqueue(&running_list, proc);
#endif

	pthread_mutex_unlock(&queue_lock);
}

/* Clean up the scheduler */
void finish_scheduler(void)
{
#ifdef CFS_SCHED
	free_rb_tree(root);
	root = NULL;
	leftmost = NULL;
#endif
	pthread_mutex_destroy(&queue_lock);
	running_list.size = 0;
}