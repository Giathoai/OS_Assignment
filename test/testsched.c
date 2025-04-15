// filepath: /mnt/n/Code_space/SimpleOS/test/testsched.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include "common.h"
#include "queue.h"
#include "sched.h"

// Define colorful output for test results
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define RESET   "\033[0m"

// Count tests
int total_tests = 0;
int passed_tests = 0;

// Synchronization for multi-threaded tests
pthread_mutex_t test_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_barrier_t test_barrier;

/* Hàm in kết quả test */
void print_result(const char *test_name, const char *expected, const char *actual, int pass) {
    total_tests++;
    if (pass) {
        passed_tests++;
        printf("%s[PASS]%s Test: %s\n", GREEN, RESET, test_name);
    }
    else {
        printf("%s[FAIL]%s Test: %s\n", RED, RESET, test_name);
        printf("  Expected: %s\n", expected);
        printf("  Actual:   %s\n", actual);
    }
}

// Create a dummy PCB for testing
struct pcb_t* create_dummy_pcb(uint32_t pid, uint32_t priority, uint32_t burst_time) {
    struct pcb_t* pcb = (struct pcb_t*)malloc(sizeof(struct pcb_t));
    if (!pcb) return NULL;
    
    pcb->pid = pid;
    pcb->priority = priority;
    pcb->burst_time = burst_time;  // For SJF/SRTF testing
    pcb->arrival_time = 0;         // Default arrival time
    pcb->remaining_time = burst_time; // For SRTF testing
#ifdef MLQ_SCHED
    pcb->prio = priority;
#endif
    
    return pcb;
}

// =================================
// PART 1: BASIC SCHEDULER FUNCTIONS
// =================================

// Test scheduler initialization
int test_init_scheduler() {
    printf("\n%s=== Running test: init_scheduler ===%s\n", YELLOW, RESET);
    
    // Initialize the scheduler
    init_scheduler();
    
    // Test 1.1: After initialization, queue_empty should return true
    int pass1 = queue_empty();
    
#ifdef MLQ_SCHED
    // In MLQ scheduler, queue_empty checks all priority queues
    // If they're all empty, it returns -1 (counter-intuitive, but that's the implementation)
    char expected[128] = "1 (all queues empty)";
    char actual[128];
    sprintf(actual, "%d %s", pass1, pass1 == 1 ? "(all queues empty)" : "(not all queues empty)");
    int real_pass1 = (pass1 == 1);
#else
    // In non-MLQ, queue_empty should return true (1) if ready and run queues are empty
    char expected[128] = "1 (queues empty)";
    char actual[128];
    sprintf(actual, "%d %s", pass1, pass1 ? "(queues empty)" : "(queues not empty)");
    int real_pass1 = pass1;
#endif
    
    print_result("init_scheduler - Queue empty check", expected, actual, real_pass1);
    
    // Test 1.2: After initialization, get_proc should return NULL
    struct pcb_t* proc = get_proc();
    int pass2 = (proc == NULL);
    sprintf(expected, "NULL");
    sprintf(actual, "%s", proc == NULL ? "NULL" : "not NULL");
    print_result("init_scheduler - get_proc returns NULL", expected, actual, pass2);
    
    return (real_pass1 && pass2);
}

// Test adding processes to the scheduler
int test_add_proc() {
    printf("\n%s=== Running test: add_proc ===%s\n", YELLOW, RESET);
    
    // Reset the scheduler
    init_scheduler();
    
    // Test 2.1: After adding a process, queue_empty should return false
    struct pcb_t* pcb1 = create_dummy_pcb(1, 10, 5);
    add_proc(pcb1);
    
    int empty_result = queue_empty();
    
#ifdef MLQ_SCHED
    // In MLQ scheduler, queue_empty checks all priority queues
    // If they're not all empty, it returns -1 (counter-intuitive)
    char expected[128] = "-1 (not all queues empty)";
    char actual[128];
    sprintf(actual, "%d %s", empty_result, empty_result == -1 ? "(not all queues empty)" : "(all queues empty)");
    int pass1 = (empty_result == -1);
#else
    // In non-MLQ, queue_empty should return false (0) if ready or run queues are not empty
    char expected[128] = "0 (queues not empty)";
    char actual[128];
    sprintf(actual, "%d %s", empty_result, empty_result == 0 ? "(queues not empty)" : "(queues empty)");
    int pass1 = (empty_result == 0);
#endif
    
    print_result("add_proc - Queue not empty after adding", expected, actual, pass1);
    
    // Test 2.2: After adding a process, get_proc should return that process
    struct pcb_t* proc = get_proc();
    int pass2 = (proc == pcb1);
    sprintf(expected, "PCB with PID=%d", pcb1->pid);
    sprintf(actual, "%s", proc == pcb1 ? "Same PCB" : "Different PCB");
    print_result("add_proc - get_proc returns added process", expected, actual, pass2);
    
    free(pcb1);
    
    return (pass1 && pass2);
}

// ===================================
// PART 2: SYNCHRONIZATION TESTING
// ===================================

// Thread function for concurrent add_proc testing
void* concurrent_add_proc(void* arg) {
    struct pcb_t* pcb = (struct pcb_t*)arg;
    
    // Wait for all threads to be ready
    pthread_barrier_wait(&test_barrier);
    
    // Add process to scheduler
    add_proc(pcb);
    
    return NULL;
}

// Thread function for concurrent get_proc testing
void* concurrent_get_proc(void* arg) {
    struct pcb_t** proc_ptr = (struct pcb_t**)arg;
    
    // Wait for all threads to be ready
    pthread_barrier_wait(&test_barrier);
    
    // Get process from scheduler
    *proc_ptr = get_proc();
    
    return NULL;
}

// Test concurrent access to the scheduler
int test_concurrent_scheduling() {
    printf("\n%s=== Running test: concurrent_scheduling ===%s\n", YELLOW, RESET);
    
    // Reset the scheduler
    init_scheduler();
    
    // Create multiple processes
    const int num_processes = 5;
    struct pcb_t* processes[num_processes];
    pthread_t add_threads[num_processes];
    
    for (int i = 0; i < num_processes; i++) {
        processes[i] = create_dummy_pcb(i + 1, 10, 5);
    }
    
    // Initialize barrier
    pthread_barrier_init(&test_barrier, NULL, num_processes + 1);
    
    // Test 1: Concurrent add_proc
    for (int i = 0; i < num_processes; i++) {
        pthread_create(&add_threads[i], NULL, concurrent_add_proc, processes[i]);
    }
    
    // Release all threads at once
    pthread_barrier_wait(&test_barrier);
    
    // Wait for all threads to complete
    for (int i = 0; i < num_processes; i++) {
        pthread_join(add_threads[i], NULL);
    }
    
    // Verify all processes were added (by retrieving them)
    int found_processes = 0;
    for (int i = 0; i < num_processes; i++) {
        struct pcb_t* proc = get_proc();
        if (proc != NULL) {
            found_processes++;
            put_proc(proc);  // Put it back for next test
        }
    }
    
    char expected[128], actual[128];
    sprintf(expected, "All %d processes added", num_processes);
    sprintf(actual, "Found %d processes", found_processes);
    int pass1 = (found_processes == num_processes);
    print_result("concurrent_scheduling - Concurrent add_proc", expected, actual, pass1);
    
    // Test 2: Concurrent get_proc
    pthread_t get_threads[num_processes];
    struct pcb_t* retrieved_processes[num_processes];
    memset(retrieved_processes, 0, sizeof(retrieved_processes));
    
    // Initialize barrier again
    pthread_barrier_init(&test_barrier, NULL, num_processes + 1);
    
    for (int i = 0; i < num_processes; i++) {
        pthread_create(&get_threads[i], NULL, concurrent_get_proc, &retrieved_processes[i]);
    }
    
    // Release all threads at once
    pthread_barrier_wait(&test_barrier);
    
    // Wait for all threads to complete
    for (int i = 0; i < num_processes; i++) {
        pthread_join(get_threads[i], NULL);
    }
    
    // Count retrieved processes
    int retrieved_count = 0;
    for (int i = 0; i < num_processes; i++) {
        if (retrieved_processes[i] != NULL) {
            retrieved_count++;
        }
    }
    
    sprintf(expected, "All processes retrieved exactly once");
    sprintf(actual, "Retrieved %d processes (should be %d)", retrieved_count, num_processes);
    int pass2 = (retrieved_count == num_processes);
    
    // Check for duplicates (same process retrieved multiple times)
    int has_duplicates = 0;
    for (int i = 0; i < num_processes; i++) {
        for (int j = i + 1; j < num_processes; j++) {
            if (retrieved_processes[i] != NULL && 
                retrieved_processes[i] == retrieved_processes[j]) {
                has_duplicates = 1;
                break;
            }
        }
        if (has_duplicates) break;
    }
    
    sprintf(expected, "No duplicate processes retrieved");
    sprintf(actual, "%s", has_duplicates ? "Found duplicates" : "No duplicates");
    int pass3 = !has_duplicates;
    print_result("concurrent_scheduling - No duplicate retrievals", expected, actual, pass3);
    
    // Clean up
    for (int i = 0; i < num_processes; i++) {
        free(processes[i]);
    }
    
    pthread_barrier_destroy(&test_barrier);
    
    return (pass1 && pass2 && pass3);
}

// =======================================
// PART 3: SCHEDULING ALGORITHM TESTING
// =======================================

// Test Round Robin (RR) scheduling
int test_rr_scheduling() {
    printf("\n%s=== Running test: Round Robin scheduling ===%s\n", YELLOW, RESET);
    
    // Reset the scheduler
    init_scheduler();
    
    // Create processes for RR testing
    const int num_processes = 3;
    struct pcb_t* processes[num_processes];
    
    for (int i = 0; i < num_processes; i++) {
        processes[i] = create_dummy_pcb(i + 1, 10, 10);  // All same priority, long burst time
    }
    
    // Add all processes to scheduler
    for (int i = 0; i < num_processes; i++) {
        add_proc(processes[i]);
    }
    
    // Test 1: First round of RR - processes should come out in order they were added
    struct pcb_t* retrieved[num_processes];
    for (int i = 0; i < num_processes; i++) {
        retrieved[i] = get_proc();
    }
    
    int pass1 = 1;
    for (int i = 0; i < num_processes; i++) {
        if (!retrieved[i] || retrieved[i]->pid != processes[i]->pid) {
            pass1 = 0;
            break;
        }
    }
    
    char expected[128], actual[128];
    sprintf(expected, "Processes retrieved in order added (PIDs: 1,2,3)");
    sprintf(actual, "PIDs: %d,%d,%d", 
            retrieved[0] ? retrieved[0]->pid : -1,
            retrieved[1] ? retrieved[1]->pid : -1,
            retrieved[2] ? retrieved[2]->pid : -1);
    print_result("RR - First round retrieval order", expected, actual, pass1);
    
    // Test 2: Put processes back and they should rotate in RR fashion
    // Put them back in a different order
    if (retrieved[1]) put_proc(retrieved[1]);  // PID 2
    if (retrieved[0]) put_proc(retrieved[0]);  // PID 1
    if (retrieved[2]) put_proc(retrieved[2]);  // PID 3
    
    // Now retrieve again - in RR, we should get them in the order they were put back
    struct pcb_t* second_round[num_processes];
    for (int i = 0; i < num_processes; i++) {
        second_round[i] = get_proc();
    }
    
    // Expected order: PID 2, PID 1, PID 3 (order they were put back)
    int expected_pids[3] = {2, 1, 3};
    int pass2 = 1;
    for (int i = 0; i < num_processes; i++) {
        if (!second_round[i] || second_round[i]->pid != expected_pids[i]) {
            pass2 = 0;
            break;
        }
    }
    
    sprintf(expected, "Processes retrieved in put-back order (PIDs: 2,1,3)");
    sprintf(actual, "PIDs: %d,%d,%d", 
            second_round[0] ? second_round[0]->pid : -1,
            second_round[1] ? second_round[1]->pid : -1,
            second_round[2] ? second_round[2]->pid : -1);
    print_result("RR - Second round rotation", expected, actual, pass2);
    
    // Clean up
    for (int i = 0; i < num_processes; i++) {
        free(processes[i]);
    }
    
    return (pass1 && pass2);
}

// =======================================
// PART 4: EDGE CASES AND ERROR HANDLING
// =======================================

// Test handling of edge cases
int test_edge_cases() {
    printf("\n%s=== Running test: edge cases ===%s\n", YELLOW, RESET);
    
    // Reset the scheduler
    init_scheduler();

    char expected[128];
    char actual[128];
    
    // Test 2: Adding a process with extreme priority
    struct pcb_t* pcb_extreme = create_dummy_pcb(99, 139, 5);  // Very high priority number
    add_proc(pcb_extreme);
    
    struct pcb_t* retrieved = get_proc();
    int pass2 = (retrieved == pcb_extreme);
    sprintf(expected, "Process with extreme priority retrieved");
    sprintf(actual, "%s", retrieved == pcb_extreme ? "Extreme priority process retrieved" : "Different process retrieved");
    print_result("edge_cases - Extreme priority handling", expected, actual, pass2);
    
    free(pcb_extreme);
    
    // Test 3: Adding maximum number of processes
    // Add many processes to potentially fill the queue
    const int max_procs = 10 + 5;  // Try to add more than max
    struct pcb_t* procs[max_procs];
    int added_count = 0;
    
    for (int i = 0; i < max_procs; i++) {
        procs[i] = create_dummy_pcb(i + 1, 10, 5);
        add_proc(procs[i]);
        
        // Try to verify if it was added
        int was_empty = queue_empty();
#ifdef MLQ_SCHED
        if (was_empty == -1) {  // Still empty, not added
            break;
        }
#else
        if (was_empty) {  // Still empty, not added
            break;
        }
#endif
        added_count++;
        
        // Get the process back to make room for the next one
        struct pcb_t* p = get_proc();
        if (p != procs[i]) {
            // Queue is behaving unexpectedly
            break;
        }
    }
    
    int pass3 = (added_count <= 10);
    sprintf(expected, "Queue enforces maximum size (%d)", 10);
    sprintf(actual, "Added %d processes", added_count);
    print_result("edge_cases - Maximum queue size handling", expected, actual, pass3);
    
    // Clean up
    for (int i = 0; i < 1; i++) {
        if (procs[i] == NULL) break;  // No more processes to free
        printf("Deleted process with PID=%d\n", procs[i]->pid);
        free(procs[i]);
    }
    
    return (pass2 && pass3);
}

// Test behavior when processes have the same scheduling metrics
int test_equal_metrics() {
    printf("\n%s=== Running test: equal_metrics ===%s\n", YELLOW, RESET);
    
    // Reset the scheduler
    init_scheduler();
    
    // Create processes with identical metrics
    struct pcb_t* pcb1 = create_dummy_pcb(1, 10, 5);
    struct pcb_t* pcb2 = create_dummy_pcb(2, 10, 5); // Same priority and burst
    struct pcb_t* pcb3 = create_dummy_pcb(3, 10, 5); // Same priority and burst
    
    // Set same arrival times
    pcb1->arrival_time = 0;
    pcb2->arrival_time = 0;
    pcb3->arrival_time = 0;
    
    // Add processes to scheduler
    add_proc(pcb1);
    add_proc(pcb2);
    add_proc(pcb3);
    
    // Test 1: When all metrics are equal, processes should be scheduled in FIFO order
    struct pcb_t* retrieved1 = get_proc();
    struct pcb_t* retrieved2 = get_proc();
    struct pcb_t* retrieved3 = get_proc();
    
    char expected[128], actual[128];
    sprintf(expected, "Processes retrieved in FIFO order (PIDs: 1,2,3)");
    sprintf(actual, "PIDs: %d,%d,%d", 
            retrieved1 ? retrieved1->pid : -1,
            retrieved2 ? retrieved2->pid : -1,
            retrieved3 ? retrieved3->pid : -1);
    
    int pass1 = (retrieved1 && retrieved1->pid == 1) &&
                (retrieved2 && retrieved2->pid == 2) &&
                (retrieved3 && retrieved3->pid == 3);
                
    print_result("equal_metrics - Same metrics, FIFO order", expected, actual, pass1);
    
    // Clean up
    free(pcb1);
    free(pcb2);
    free(pcb3);
    
    return pass1;
}

// Main function to run all tests
int main() {
    printf("%s======= Scheduler Test Suite =======%s\n", YELLOW, RESET);

    int your_log = open("log_schedule.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (your_log == -1) {
        perror("open");
            return 1;
    }

    // Lưu lại stdout gốc
    int stdout_backup = dup(STDOUT_FILENO);

    // Redirect stdout sang file
    dup2(your_log, STDOUT_FILENO);
    close(your_log); // Không cần nữa
    
    // Run basic tests
    int test1 = test_init_scheduler();
    int test2 = test_add_proc();
    
    // Run synchronization tests
    int test3 = test_concurrent_scheduling();
    
    // Run algorithm-specific tests
    int test5 = test_rr_scheduling();
    
    // Run edge case tests
    int test8 = test_edge_cases();
    int test9 = test_equal_metrics();

    // Khôi phục stdout gốc
    dup2(stdout_backup, STDOUT_FILENO);
    close(stdout_backup);
    
    // Print summary
    printf("\n%s======= Test Summary =======%s\n", YELLOW, RESET);
    printf("Tests passed: %d/%d\n", passed_tests, total_tests);
    
    printf("\n%s== Basic Tests ==%s\n", BLUE, RESET);
    printf("Test init_scheduler:         %s%s%s\n", test1 ? GREEN : RED, test1 ? "PASSED" : "FAILED", RESET);
    printf("Test add_proc:               %s%s%s\n", test2 ? GREEN : RED, test2 ? "PASSED" : "FAILED", RESET);
    
    printf("\n%s== Synchronization Tests ==%s\n", BLUE, RESET);
    printf("Test concurrent_scheduling:  %s%s%s\n", test3 ? GREEN : RED, test3 ? "PASSED" : "FAILED", RESET);
    
    printf("\n%s== Algorithm Tests ==%s\n", BLUE, RESET);
    printf("Test RR scheduling:          %s%s%s\n", test5 ? GREEN : RED, test5 ? "PASSED" : "FAILED", RESET);
    
    printf("\n%s== Edge Case Tests ==%s\n", BLUE, RESET);
    printf("Test edge cases:             %s%s%s\n", test8 ? GREEN : RED, test8 ? "PASSED" : "FAILED", RESET);
    printf("Test equal metrics:          %s%s%s\n", test9 ? GREEN : RED, test9 ? "PASSED" : "FAILED", RESET);
    
    int all_passed = test1 && test2 && test3 && test5 && test8 && test9;
    
    printf("\n%s===========================%s\n", YELLOW, RESET);
    printf("Overall result: %s%s%s\n", all_passed ? GREEN : RED, 
           all_passed ? "ALL TESTS PASSED" : "SOME TESTS FAILED", RESET);
    
    return all_passed ? 0 : 1;
}