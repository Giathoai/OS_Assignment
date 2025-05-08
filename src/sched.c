#include "queue.h"
#include "rbtree.h"
#include "sched.h"
#include <pthread.h>

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
static struct queue_t ready_queue;
static struct queue_t run_queue;
static pthread_mutex_t queue_lock;

static struct queue_t running_list;
#ifdef MLQ_SCHED
static struct queue_t mlq_ready_queue[MAX_PRIO];
static int slot[MAX_PRIO];
#else
static struct RedBlackTree cfs_rbtree;
static pthread_mutex_t cfs_lock;
#define TARGET_LATENCY 20000;
#endif

int queue_empty(void) {
#ifdef MLQ_SCHED
	unsigned long prio;
	for (prio = 0; prio < MAX_PRIO; prio++)
		if(!empty(&mlq_ready_queue[prio])) 
			return -1;
#endif
	return (empty(&ready_queue) && empty(&run_queue));
}

// void init_scheduler(void) {
// #ifdef MLQ_SCHED
//     int i ;

// 	for (i = 0; i < MAX_PRIO; i ++) {
// 		mlq_ready_queue[i].size = 0;
// 		slot[i] = MAX_PRIO - i; 
// 	}
// #endif
// 	ready_queue.size = 0;
// 	run_queue.size = 0;
// 	pthread_mutex_init(&queue_lock, NULL);
// }

#ifdef MLQ_SCHED
/* 
 *  Stateful design for routine calling
 *  based on the priority and our MLQ policy
 *  We implement stateful here using transition technique
 *  State representation   prio = 0 .. MAX_PRIO, curr_slot = 0..(MAX_PRIO - prio)
 */
struct pcb_t * get_mlq_proc(void) {
	/*TODO: get a process from PRIORITY [ready_queue].
	 * Remember to use lock to protect the queue.
	 * */
	pthread_mutex_lock(&queue_lock);
	struct pcb_t * proc = NULL;
	for (int i = 0; i < MAX_PRIO; i++) {
		if (slot[i] > 0) {
			proc = dequeue(&mlq_ready_queue[i]);
			if (proc != NULL) {
				slot[i]--;
				pthread_mutex_unlock(&queue_lock);
				return proc;
			}
		}
	}
	for (int i = 0; i < MAX_PRIO; i++) {
		slot[i] = MAX_PRIO - i;
	}
	pthread_mutex_unlock(&queue_lock);
	return NULL;
}

void put_mlq_proc(struct pcb_t * proc) {
	pthread_mutex_lock(&queue_lock);
	enqueue(&mlq_ready_queue[proc->prio], proc);
	pthread_mutex_unlock(&queue_lock);
}

void add_mlq_proc(struct pcb_t * proc) {
	pthread_mutex_lock(&queue_lock);
	enqueue(&mlq_ready_queue[proc->prio], proc);
	pthread_mutex_unlock(&queue_lock);	
}

struct pcb_t * get_proc(void) {
	return get_mlq_proc();
}

void put_proc(struct pcb_t * proc) {
	proc->ready_queue = &ready_queue;
	proc->mlq_ready_queue = mlq_ready_queue;
	proc->running_list = & running_list;

	/* TODO: put running proc to running_list */
	pthread_mutex_lock(&queue_lock);
	enqueue(&running_list, proc);
	pthread_mutex_unlock(&queue_lock);

	return put_mlq_proc(proc);
}

void add_proc(struct pcb_t * proc) {
	proc->ready_queue = &ready_queue;
	proc->mlq_ready_queue = mlq_ready_queue;
	proc->running_list = & running_list;

	/* TODO: put running proc to running_list */
	pthread_mutex_lock(&queue_lock);
	enqueue(&running_list, proc);
	pthread_mutex_unlock(&queue_lock);

	return add_mlq_proc(proc);
}
#else
int tree_empty(void) {
    return cfs_rbtree.root == NULL;
}

void init_scheduler(void) {
    initTree(&cfs_rbtree, compareDouble, NULL);
    pthread_mutex_init(&cfs_lock, NULL);
}

double calculateWeight(uint32_t nice) {
    return (1024.0 * pow(2, -1.0 * (double)nice / 10.0));
}

static double concac = 0;
double get_total_weight(Node *node) {
    if (!node) return 0;
    struct pcb_t *pcb = (struct pcb_t *)node->data;
	concac += calculateWeight(pcb->priority);
    return calculateWeight(pcb->priority) + get_total_weight(node->left) + get_total_weight(node->right);
}

double calculate_timeslice(struct pcb_t *proc) {
	double total_weight = get_total_weight(cfs_rbtree.root);
    double proc_weight = calculateWeight(proc->priority);
    if (total_weight == 0) return 0;
    return (proc_weight / total_weight) * 0.02;
}

Node* find_min_vruntime_node(Node *node) {
    if (!node) return NULL;
    while (node->left) node = node->left;
    return node;
}

void update_vruntime(struct pcb_t *proc, int run_time) {
	double delta = (double)run_time;
	double weight;
	
	weight = calculateWeight(proc->priority);
	delta = delta / (double)weight;
	proc->vruntime += delta;
}

struct pcb_t *get_proc(void) {
    pthread_mutex_lock(&cfs_lock);

    Node *min_node = find_min_vruntime_node(cfs_rbtree.root);
    if (!min_node) {
        pthread_mutex_unlock(&cfs_lock);
        return NULL;
    }
    struct pcb_t *proc = (struct pcb_t *)min_node->data;
	//proc->vruntime += proc->timeslice * (1.0 / calculateWeight(proc->priority));
    proc->timeslice = calculate_timeslice(proc);
    cfs_rbtree.root = delete_node(&cfs_rbtree, min_node, proc, cfs_rbtree.cmp);
    

    pthread_mutex_unlock(&cfs_lock);
    return proc;
}


void put_proc(struct pcb_t *proc) {
    pthread_mutex_lock(&cfs_lock);
	insert(&cfs_rbtree, proc);
    pthread_mutex_unlock(&cfs_lock);
}

void add_proc(struct pcb_t *proc) {
    pthread_mutex_lock(&cfs_lock);
	proc->vruntime = 0;
	proc->timeslice = calculate_timeslice(proc);
    insert(&cfs_rbtree, proc);
    pthread_mutex_unlock(&cfs_lock);
}

#endif  // End of CFS scheduler section

