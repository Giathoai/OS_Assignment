#include "queue.h"
#include "sched.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Static queues for compatibility
static struct queue_t ready_queue;
static struct queue_t run_queue;
static pthread_mutex_t queue_lock;

static struct queue_t running_list;

#ifdef CFS_SCHED
// Red-black tree node for CFS
struct rb_node
{
	struct pcb_t *proc;
	unsigned long vruntime; // Key for sorting
	struct rb_node *left, *right, *parent;
	int color; // 0 = black, 1 = red
};

static struct rb_node *rb_root = NULL;

// Priority-to-weight mapping (based on Linux nice values, scaled for simplicity)
static const int prio_to_weight[MAX_PRIO] = {
	// prio 0 (highest) to 139 (lowest)
	1024, 1000, 976, 952, 928, 904, 880, 856, 832, 808, // 0-9
	784, 760, 736, 712, 688, 664, 640, 616, 592, 568,	// 10-19
	544, 520, 496, 472, 448, 424, 400, 376, 352, 328,	// 20-29
	304, 280, 256, 232, 208, 184, 160, 136, 112, 88,	// 30-39
	64, 60, 56, 52, 48, 44, 40, 36, 32, 28,				// 40-49
	// Continue with weight = 15 for prio 50-139 (simplified)
	[50 ... 139] = 15};

// Minimum vruntime to prevent wraparound
static unsigned long min_vruntime = 0;

// Red-black tree operations
static struct rb_node *rb_create_node(struct pcb_t *proc, unsigned long vruntime)
{
	struct rb_node *node = (struct rb_node *)malloc(sizeof(struct rb_node));
	node->proc = proc;
	node->vruntime = vruntime;
	node->left = node->right = node->parent = NULL;
	node->color = 1; // New nodes are red
	return node;
}

static void rb_rotate_left(struct rb_node **root, struct rb_node *x)
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

static void rb_rotate_right(struct rb_node **root, struct rb_node *y)
{
	struct rb_node *x = y->left;
	y->left = x->right;
	if (x->right)
		x->right->parent = y;
	x->parent = y->parent;
	if (!y->parent)
		*root = x;
	else if (y == y->parent->right)
		y->parent->right = x;
	else
		y->parent->left = x;
	x->right = y;
	y->parent = x;
}

static void rb_insert_fixup(struct rb_node **root, struct rb_node *z)
{
	while (z->parent && z->parent->color == 1)
	{
		if (z->parent == z->parent->parent->left)
		{
			struct rb_node *y = z->parent->parent->right;
			if (y && y->color == 1)
			{
				z->parent->color = 0;
				y->color = 0;
				z->parent->parent->color = 1;
				z = z->parent->parent;
			}
			else
			{
				if (z == z->parent->right)
				{
					z = z->parent;
					rb_rotate_left(root, z);
				}
				z->parent->color = 0;
				z->parent->parent->color = 1;
				rb_rotate_right(root, z->parent->parent);
			}
		}
		else
		{
			struct rb_node *y = z->parent->parent->left;
			if (y && y->color == 1)
			{
				z->parent->color = 0;
				y->color = 0;
				z->parent->parent->color = 1;
				z = z->parent->parent;
			}
			else
			{
				if (z == z->parent->left)
				{
					z = z->parent;
					rb_rotate_right(root, z);
				}
				z->parent->color = 0;
				z->parent->parent->color = 1;
				rb_rotate_left(root, z->parent->parent);
			}
		}
	}
	(*root)->color = 0;
}

static void rb_insert(struct rb_node **root, struct pcb_t *proc, unsigned long vruntime)
{
	struct rb_node *z = rb_create_node(proc, vruntime);
	struct rb_node *y = NULL;
	struct rb_node *x = *root;

	while (x)
	{
		y = x;
		if (z->vruntime < x->vruntime)
			x = x->left;
		else
			x = x->right;
	}

	z->parent = y;
	if (!y)
		*root = z;
	else if (z->vruntime < y->vruntime)
		y->left = z;
	else
		y->right = z;

	rb_insert_fixup(root, z);
}

static struct rb_node *rb_min(struct rb_node *node)
{
	while (node && node->left)
		node = node->left;
	return node;
}

static void rb_free(struct rb_node *node)
{
	if (!node)
		return;
	rb_free(node->left);
	rb_free(node->right);
	free(node);
}

static int rb_empty(void)
{
	return rb_root == NULL;
}

static struct pcb_t *rb_get_min_proc(void)
{
	pthread_mutex_lock(&queue_lock);
	struct rb_node *min_node = rb_min(rb_root);
	if (!min_node)
	{
		pthread_mutex_unlock(&queue_lock);
		return NULL;
	}

	struct pcb_t *proc = min_node->proc;

	// Simple deletion: replace with right child or NULL (not fully balanced for simplicity)
	if (min_node->right)
	{
		min_node->right->parent = min_node->parent;
		if (!min_node->parent)
			rb_root = min_node->right;
		else if (min_node == min_node->parent->left)
			min_node->parent->left = min_node->right;
		else
			min_node->parent->right = min_node->right;
	}
	else
	{
		if (!min_node->parent)
			rb_root = NULL;
		else if (min_node == min_node->parent->left)
			min_node->parent->left = NULL;
		else
			min_node->parent->right = NULL;
	}

	free(min_node);
	pthread_mutex_unlock(&queue_lock);
	return proc;
}

static void update_min_vruntime(void)
{
	struct rb_node *min_node = rb_min(rb_root);
	if (min_node)
		min_vruntime = min_node->vruntime;
	else
		min_vruntime += 1000; // Arbitrary increment when tree is empty
}
#endif

int queue_empty(void)
{
#ifdef CFS_SCHED
	return rb_empty() && empty(&ready_queue) && empty(&run_queue);
#else
	return empty(&ready_queue) && empty(&run_queue);
#endif
}

void init_scheduler(void)
{
#ifdef CFS_SCHED
	rb_root = NULL;
	min_vruntime = 0;
#endif
	ready_queue.size = 0;
	run_queue.size = 0;
	running_list.size = 0;
	pthread_mutex_init(&queue_lock, NULL);
}

void finish_scheduler(void)
{
#ifdef CFS_SCHED
	rb_free(rb_root);
	rb_root = NULL;
#endif
	pthread_mutex_destroy(&queue_lock);
}

#ifdef CFS_SCHED
struct pcb_t *get_proc(void)
{
	struct pcb_t *proc = rb_get_min_proc();
	if (proc)
	{
		// Add to running_list for tracking
		pthread_mutex_lock(&queue_lock);
		enqueue(&running_list, proc);
		pthread_mutex_unlock(&queue_lock);
	}
	return proc;
}

void put_proc(struct pcb_t *proc)
{
	proc->ready_queue = &ready_queue;
	proc->running_list = &running_list;

	// Update vruntime (assume fixed time slice of 10ms for simplicity)
	unsigned long delta_exec = 10000; // 10ms in ns
	int weight = prio_to_weight[proc->prio];
	proc->vruntime += (delta_exec * 1024) / weight; // Scale by weight

	// Update min_vruntime
	update_min_vruntime();

	// Re-insert into red-black tree
	pthread_mutex_lock(&queue_lock);
	rb_insert(&rb_root, proc, proc->vruntime);
	pthread_mutex_unlock(&queue_lock);
}

void add_proc(struct pcb_t *proc)
{
	proc->ready_queue = &ready_queue;
	proc->running_list = &running_list;

	// Initialize vruntime and weight
	proc->vruntime = min_vruntime;
	proc->weight = prio_to_weight[proc->prio];

	// Add to red-black tree
	pthread_mutex_lock(&queue_lock);
	rb_insert(&rb_root, proc, proc->vruntime);
	enqueue(&running_list, proc); // Track in running_list
	pthread_mutex_unlock(&queue_lock);
}
#else
struct pcb_t *get_proc(void)
{
	struct pcb_t *proc = NULL;
	pthread_mutex_lock(&queue_lock);
	proc = dequeue(&ready_queue);
	if (proc)
		enqueue(&running_list, proc);
	pthread_mutex_unlock(&queue_lock);
	return proc;
}

void put_proc(struct pcb_t *proc)
{
	proc->ready_queue = &ready_queue;
	proc->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	enqueue(&run_queue, proc);
	enqueue(&running_list, proc);
	pthread_mutex_unlock(&queue_lock);
}

void add_proc(struct pcb_t *proc)
{
	proc->ready_queue = &ready_queue;
	proc->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	enqueue(&ready_queue, proc);
	enqueue(&running_list, proc);
	pthread_mutex_unlock(&queue_lock);
}
#endif