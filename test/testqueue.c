#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include "common.h"
#include "queue.h"

// Define colorful output for test results
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define RESET   "\033[0m"

// Count tests
int total_tests = 0;
int passed_tests = 0;

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
struct pcb_t* create_dummy_pcb(uint32_t pid, uint32_t priority) {
    struct pcb_t* pcb = (struct pcb_t*)malloc(sizeof(struct pcb_t));
    if (!pcb) return NULL;
    
    pcb->pid = pid;
    pcb->priority = priority;
#ifdef MLQ_SCHED
    pcb->prio = priority;
#endif
    
    return pcb;
}

// Test empty queue function
int test_empty_queue() {
    printf("\n%s=== Running test: empty queue ===%s\n", YELLOW, RESET);
    
    // Test 1.1: NULL queue should be considered empty
    int pass1 = empty(NULL);
    print_result("empty - NULL queue", "1 (empty)", pass1 ? "1 (empty)" : "0 (not empty)", pass1);
    
    // Test 1.2: Empty queue should return 1
    struct queue_t empty_q;
    empty_q.size = 0;
    int pass2 = empty(&empty_q);
    print_result("empty - Empty queue", "1 (empty)", pass2 ? "1 (empty)" : "0 (not empty)", pass2);
    
    // Test 1.3: Non-empty queue should return 0
    struct queue_t non_empty_q;
    non_empty_q.size = 1;
    int pass3 = !empty(&non_empty_q);
    print_result("empty - Non-empty queue", "0 (not empty)", !pass3 ? "0 (not empty)" : "1 (empty)", pass3);
    
    return (pass1 && pass2 && pass3);
}

// Test enqueue function
int test_enqueue() {
    printf("\n%s=== Running test: enqueue ===%s\n", YELLOW, RESET);
    
    struct queue_t queue;
    queue.size = 0;
    
    // Test 2.1: Enqueue to empty queue
    struct pcb_t* pcb1 = create_dummy_pcb(1, 1);
    enqueue(&queue, pcb1);
    int pass1 = (queue.size == 1 && queue.proc[0] == pcb1);
    char expected[128], actual[128];
    sprintf(expected, "size=1, proc[0]=pcb1");
    sprintf(actual, "size=%d, proc[0]=%s", queue.size, (queue.size > 0 && queue.proc[0] == pcb1) ? "pcb1" : "wrong pcb");
    print_result("enqueue - Empty queue", expected, actual, pass1);
    
    // Test 2.2: Enqueue to non-empty queue
    struct pcb_t* pcb2 = create_dummy_pcb(2, 2);
    enqueue(&queue, pcb2);
    int pass2 = (queue.size == 2 && queue.proc[1] == pcb2);
    sprintf(expected, "size=2, proc[1]=pcb2");
    sprintf(actual, "size=%d, proc[1]=%s", queue.size, (queue.size > 1 && queue.proc[1] == pcb2) ? "pcb2" : "wrong pcb");
    print_result("enqueue - Non-empty queue", expected, actual, pass2);
    
    // Test 2.3: Enqueue to full queue (should not add)
    queue.size = MAX_QUEUE_SIZE;
    struct pcb_t* pcb3 = create_dummy_pcb(3, 3);
    enqueue(&queue, pcb3);
    int pass3 = (queue.size == MAX_QUEUE_SIZE); // Size should remain the same
    sprintf(expected, "size=%d", MAX_QUEUE_SIZE);
    sprintf(actual, "size=%d", queue.size);
    print_result("enqueue - Full queue", expected, actual, pass3);
    
    // Cleanup
    free(pcb1);
    free(pcb2);
    free(pcb3);
    
    return (pass1 && pass2 && pass3);
}

// Test dequeue function
int test_dequeue() {
    printf("\n%s=== Running test: dequeue ===%s\n", YELLOW, RESET);
    
    // Test 3.1: Dequeue from empty queue
    struct queue_t empty_queue;
    empty_queue.size = 0;
    struct pcb_t* result1 = dequeue(&empty_queue);
    int pass1 = (result1 == NULL);
    char expected[128], actual[128];
    sprintf(expected, "NULL");
    sprintf(actual, "%s", result1 == NULL ? "NULL" : "not NULL");
    print_result("dequeue - Empty queue", expected, actual, pass1);
    
    // Test 3.2: Dequeue from queue with one element
    struct queue_t single_queue;
    single_queue.size = 1;
    struct pcb_t* pcb1 = create_dummy_pcb(1, 1);
#ifdef MLQ_SCHED
    pcb1->prio = 5; // Lower priority number means higher priority
#endif
    single_queue.proc[0] = pcb1;
    
    struct pcb_t* result2 = dequeue(&single_queue);
    int pass2 = (result2 == pcb1 && single_queue.size == 0);
    sprintf(expected, "pcb1, new size=0");
    sprintf(actual, "%s, new size=%d", result2 == pcb1 ? "pcb1" : "wrong pcb", single_queue.size);
    print_result("dequeue - Single element queue", expected, actual, pass2);
    
    // Test 3.3: Dequeue from queue with multiple elements (priority based)
    struct queue_t multi_queue;
    multi_queue.size = 3;
    
    struct pcb_t* pcb2 = create_dummy_pcb(2, 10);
    struct pcb_t* pcb3 = create_dummy_pcb(3, 5);  // Higher priority (lower number)
    struct pcb_t* pcb4 = create_dummy_pcb(4, 15);
    
#ifdef MLQ_SCHED
    pcb2->prio = 10;
    pcb3->prio = 5;   // Highest priority (lowest number)
    pcb4->prio = 15;
#endif
    
    multi_queue.proc[0] = pcb2;
    multi_queue.proc[1] = pcb3;
    multi_queue.proc[2] = pcb4;
    
    struct pcb_t* result3 = dequeue(&multi_queue);
    
#ifdef MLQ_SCHED
    // In MLQ scheduler, it should return the highest priority process (pcb3)
    int pass3 = (result3 == pcb3 && multi_queue.size == 2);
    sprintf(expected, "pcb3 (prio=5), new size=2");
    sprintf(actual, "%s, new size=%d", 
            result3 == pcb3 ? "pcb3 (prio=5)" : 
            result3 == pcb2 ? "pcb2 (prio=10)" : 
            result3 == pcb4 ? "pcb4 (prio=15)" : "wrong pcb", 
            multi_queue.size);
#else
    // In non-MLQ, it might default to FIFO, so it would return pcb2
    int pass3 = (result3 == pcb2 && multi_queue.size == 2);
    sprintf(expected, "pcb2, new size=2");
    sprintf(actual, "%s, new size=%d", 
            result3 == pcb2 ? "pcb2" : 
            result3 == pcb3 ? "pcb3" : 
            result3 == pcb4 ? "pcb4" : "wrong pcb", 
            multi_queue.size);
#endif
    
    print_result("dequeue - Multiple element queue (priority)", expected, actual, pass3);
    
    // Cleanup
    free(pcb1);
    free(pcb2);
    free(pcb3);
    free(pcb4);
    
    return (pass1 && pass2 && pass3);
}

// Main function to run all tests
int main() {
    printf("%s======= Queue Test Suite =======%s\n", YELLOW, RESET);
    int your_log = open("log_queue.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (your_log == -1) {
        perror("open");
            return 1;
    }

    // Lưu lại stdout gốc
    int stdout_backup = dup(STDOUT_FILENO);

    // Redirect stdout sang file
    dup2(your_log, STDOUT_FILENO);
    close(your_log); // Không cần nữa
    
    // Run all the tests
    int test1 = test_empty_queue();
    int test2 = test_enqueue();
    int test3 = test_dequeue();

    // Khôi phục stdout gốc
    dup2(stdout_backup, STDOUT_FILENO);
    close(stdout_backup);
    
    // Print summary
    printf("\n%s======= Test Summary =======%s\n", YELLOW, RESET);
    printf("Tests passed: %d/%d\n", passed_tests, total_tests);
    
    printf("\nTest empty queue:   %s%s%s\n", test1 ? GREEN : RED, test1 ? "PASSED" : "FAILED", RESET);
    printf("Test enqueue:       %s%s%s\n", test2 ? GREEN : RED, test2 ? "PASSED" : "FAILED", RESET);
    printf("Test dequeue:       %s%s%s\n", test3 ? GREEN : RED, test3 ? "PASSED" : "FAILED", RESET);
    
    int all_passed = test1 && test2 && test3;
    
    printf("\n%s===========================%s\n", YELLOW, RESET);
    printf("Overall result: %s%s%s\n", all_passed ? GREEN : RED, 
           all_passed ? "ALL TESTS PASSED" : "SOME TESTS FAILED", RESET);
    
    return all_passed ? 0 : 1;
}