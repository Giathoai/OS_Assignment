#include "queue.h"
#include "sched.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

/* CFS scheduler implementation */
#define CFS_SCHED

static struct queue_t ready_queue;
static struct queue_t run_queue;
static pthread_mutex_t queue_lock;

static struct queue_t running_list;

/* CFS specific data structures */
#ifdef CFS_SCHED
#define DEFAULT_WEIGHT 1024 // Default process weight
#define MIN_VRUNTIME 0		// Minimum virtual runtime

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
	if (node)
	{
		node->proc = proc;
		node->vruntime = proc->prio ? MIN_VRUNTIME + (DEFAULT_WEIGHT / proc->prio) : MIN_VRUNTIME;
		node->left = NULL;
		node->right = NULL;
		node->parent = NULL;
		node->color = 1; // New nodes are red
	}
	return node;
}

static void rb_insert_fixup(struct rb_node **root, struct rb_node *node);

/* Insert a node into the Red-Black Tree */
static void rb_insert(struct rb_node **root, struct rb_node *node)
{
	struct rb_node *y = NULL;
	struct rb_node *x = *root;

	// Find the position to insert
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

	// Update leftmost if needed
	if (leftmost == NULL || node->vruntime < leftmost->vruntime)
		leftmost = node;
}

/* Fix Red-Black Tree properties after insertion */
static void rb_insert_fixup(struct rb_node **root, struct rb_node *node)
{
	struct rb_node *parent, *grandparent, *uncle;

	// While node's parent is red
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

					// Left rotation
					struct rb_node *right = node->right;
					node->right = right->left;
					if (right->left)
						right->left->parent = node;
					right->parent = node->parent;
					if (!node->parent)
						*root = right;
					else if (node == node->parent->left)
						node->parent->left = right;
					else
						node->parent->right = right;
					right->left = node;
					node->parent = right;

					parent = node->parent;
				}

				// Case 3: Uncle is black, node is left child
				parent->color = 0;
				grandparent->color = 1;

				// Right rotation
				struct rb_node *left = grandparent->left;
				grandparent->left = left->right;
				if (left->right)
					left->right->parent = grandparent;
				left->parent = grandparent->parent;
				if (!grandparent->parent)
					*root = left;
				else if (grandparent == grandparent->parent->left)
					grandparent->parent->left = left;
				else
					grandparent->parent->right = left;
				left->right = grandparent;
				grandparent->parent = left;
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

					// Right rotation
					struct rb_node *left = node->left;
					node->left = left->right;
					if (left->right)
						left->right->parent = node;
					left->parent = node->parent;
					if (!node->parent)
						*root = left;
					else if (node == node->parent->left)
						node->parent->left = left;
					else
						node->parent->right = left;
					left->right = node;
					node->parent = left;

					parent = node->parent;
				}

				// Case 3: Uncle is black, node is right child
				parent->color = 0;
				grandparent->color = 1;

				// Left rotation
				struct rb_node *right = grandparent->right;
				grandparent->right = right->left;
				if (right->left)
					right->left->parent = grandparent;
				right->parent = grandparent->parent;
				if (!grandparent->parent)
					*root = right;
				else if (grandparent == grandparent->parent->left)
					grandparent->parent->left = right;
				else
					grandparent->parent->right = right;
				right->left = grandparent;
				grandparent->parent = right;
			}
		}
	}

	// Root is always black
	(*root)->color = 0;
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

/* Remove a node from Red-Black Tree */
static void rb_remove(struct rb_node **root, struct rb_node *node)
{
	struct rb_node *child, *parent;
	int color;

	// If both children are present
	if (node->left && node->right)
	{
		struct rb_node *successor = rb_find_min(node->right);

		// Copy successor's data to node
		node->proc = successor->proc;
		node->vruntime = successor->vruntime;

		// Remove successor
		node = successor;
	}

	// At this point, node has at most one child
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

	free(node);

	// Fix Red-Black properties if needed
	if (color == 0)
	{
		// Implement rb_remove_fixup if needed
		// For simplicity, we'll skip this part for now
	}
}

/* Update the vruntime of a process based on its execution time */
static void update_vruntime(struct rb_node *node, int execution_time)
{
	if (!node)
		return;

	unsigned long weight = node->proc->prio ? DEFAULT_WEIGHT / node->proc->prio : DEFAULT_WEIGHT;
	node->vruntime += execution_time * weight;
}
#endif

int queue_empty(void)
{
#ifdef CFS_SCHED
	return (root == NULL);
#else
	return (empty(&ready_queue) && empty(&run_queue));
#endif
}

void init_scheduler(void)
{
#ifdef CFS_SCHED
	root = NULL;
	leftmost = NULL;
#else
	ready_queue.size = 0;
	run_queue.size = 0;
#endif
	pthread_mutex_init(&queue_lock, NULL);
	running_list.size = 0;
}

struct pcb_t *get_proc(void)
{
#ifdef CFS_SCHED
	pthread_mutex_lock(&queue_lock);

	if (!root)
	{
		pthread_mutex_unlock(&queue_lock);
		return NULL;
	}

	// Get the process with the minimum vruntime
	struct rb_node *node = leftmost;
	struct pcb_t *proc = NULL;

	if (node)
	{
		proc = node->proc;
		rb_remove(&root, node);
	}

	pthread_mutex_unlock(&queue_lock);
	return proc;
#else
	struct pcb_t *proc = NULL;
	/*TODO: get a process from [ready_queue].
	 * Remember to use lock to protect the queue.
	 * */
	return proc;
#endif
}

void put_proc(struct pcb_t *proc)
{
	proc->ready_queue = &ready_queue;
	proc->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	enqueue(&running_list, proc);

#ifdef CFS_SCHED
	// Update the vruntime of the process based on some metric
	// For simplicity, we'll use a fixed execution time
	int execution_time = 1;

	// Create a new node for the process
	struct rb_node *node = rb_new_node(proc);

	// Insert the node into the Red-Black Tree
	rb_insert(&root, node);
#else
	enqueue(&run_queue, proc);
#endif

	pthread_mutex_unlock(&queue_lock);
}

void add_proc(struct pcb_t *proc)
{
	proc->ready_queue = &ready_queue;
	proc->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	enqueue(&running_list, proc);

#ifdef CFS_SCHED
	// Create a new node for the process
	struct rb_node *node = rb_new_node(proc);

	// Insert the node into the Red-Black Tree
	rb_insert(&root, node);
#else
	enqueue(&ready_queue, proc);
#endif

	pthread_mutex_unlock(&queue_lock);
}

void finish_scheduler(void)
{
#ifdef CFS_SCHED
	// Free all nodes in the Red-Black Tree
	// For simplicity, we'll just set the root to NULL
	// In a real implementation, we would need to free all nodes
	root = NULL;
	leftmost = NULL;
#endif
	pthread_mutex_destroy(&queue_lock);
}