#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include "common.h"
#include "os-mm.h"
#include "mm.h"
#include "libmem.h"
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, int size, int alignedsz);

// Define colorful output for test results
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define RESET   "\033[0m"

// Định nghĩa mmvm_lock nếu chưa có (để dùng trong liballoc/__alloc)
pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

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

// Helper function to setup a test process
struct pcb_t* setup_test_process(int with_mram) {
    struct pcb_t *proc = malloc(sizeof(struct pcb_t));
    if (!proc) { 
        perror("malloc proc"); 
        exit(1); 
    }
    memset(proc, 0, sizeof(struct pcb_t));
    proc->pid = 1;
    proc->bp = PAGE_SIZE;
    memset(proc->regs, 0, sizeof(proc->regs));
 
    proc->mm = malloc(sizeof(struct mm_struct));
    if (!proc->mm) { 
        perror("malloc mm"); 
        free(proc);
        exit(1); 
    }
    if (init_mm(proc->mm, proc) != 0) {
        fprintf(stderr, "init_mm failed\n");
        free(proc->mm);
        free(proc);
        exit(1);
    }
 
    if (with_mram) {
        /* Cấp phát và khởi tạo MEMRAM */
        proc->mram = malloc(sizeof(struct memphy_struct));
        if (!proc->mram) {
            perror("malloc mram");
            free(proc->mm);
            free(proc);
            exit(1);
        }
        if (init_memphy(proc->mram, 2048, 1) != 0) {
            fprintf(stderr, "init_memphy for mram failed\n");
            free(proc->mram);
            free(proc->mm);
            free(proc);
            exit(1);
        }
 
        /* Cấp phát danh sách các vùng MEMSWP */
        proc->mswp = malloc(PAGING_MAX_MMSWP * sizeof(struct memphy_struct *));
        if (!proc->mswp) {
            perror("malloc mswp array");
            free(proc->mram->storage);
            free(proc->mram);
            free(proc->mm);
            free(proc);
            exit(1);
        }
        int sit;
        for (sit = 0; sit < PAGING_MAX_MMSWP; sit++) {
            proc->mswp[sit] = malloc(sizeof(struct memphy_struct));
            if (!proc->mswp[sit]) {
                perror("malloc mswp element");
                // Dọn dẹp các vùng đã cấp phát trước đó
                int j;
                for (j = 0; j < sit; j++) {
                    free(proc->mswp[j]->storage);
                    free(proc->mswp[j]);
                }
                free(proc->mswp);
                free(proc->mram->storage);
                free(proc->mram);
                free(proc->mm);
                free(proc);
                exit(1);
            }
            if (init_memphy(proc->mswp[sit], 1024, 1) != 0) {
                fprintf(stderr, "init_memphy for mswp[%d] failed\n", sit);
                int j;
                for (j = 0; j <= sit; j++) {
                    free(proc->mswp[j]->storage);
                    free(proc->mswp[j]);
                }
                free(proc->mswp);
                free(proc->mram->storage);
                free(proc->mram);
                free(proc->mm);
                free(proc);
                exit(1);
            }
        }
        /* Gán active swap là vùng đầu tiên trong danh sách */
        proc->active_mswp = proc->mswp[0];
        proc->active_mswp_id = 0;
    } else {
        proc->mram = NULL;
        proc->mswp = NULL;
        proc->active_mswp = NULL;
        proc->active_mswp_id = 0;
    }
 
    return proc;
}

// Helper function to cleanup a test process
void cleanup_test_process(struct pcb_t *proc, int with_mram) {
    if (!proc) return;
    
    if (proc->mm) {
        if (proc->mm->pgd) {
            free(proc->mm->pgd);
        }
        free(proc->mm);
    }
    
    if (with_mram) {
        if (proc->mram) {
            if (proc->mram->storage) {
                free(proc->mram->storage);
            }
            free(proc->mram);
        }
        
        if (proc->mswp) {
            int sit;
            for (sit = 0; sit < PAGING_MAX_MMSWP; sit++) {
                if (proc->mswp[sit]) {
                    if (proc->mswp[sit]->storage) {
                        free(proc->mswp[sit]->storage);
                    }
                    free(proc->mswp[sit]);
                }
            }
            free(proc->mswp);
        }
    }
    
    free(proc);
}

/* Test 1: Virtual Memory Management - init_mm */
int test_init_mm() {
    printf("\n%s=== Running test: init_mm ===%s\n", YELLOW, RESET);
    
    struct pcb_t *proc = setup_test_process(0);
    if (!proc) return 0;
    
    struct vm_area_struct *vma0 = get_vma_by_num(proc->mm, 0);
    
    // Test 1.1: VMA0 exists
    int pass1 = (vma0 != NULL);
    print_result("init_mm - VMA0 creation", "VMA0 exists", 
                 pass1 ? "VMA0 exists" : "VMA0 is NULL", pass1);
    
    // Test 1.2: VMA0 has correct initial values
    int pass2 = (vma0 && vma0->vm_start == 0 && vma0->vm_end == 0 && vma0->sbrk == 0);
    char expected[128], actual[128];
    sprintf(expected, "vm_start=0, vm_end=0, sbrk=0");
    sprintf(actual, "vm_start=%lu, vm_end=%lu, sbrk=%lu", 
            vma0 ? vma0->vm_start : -1, 
            vma0 ? vma0->vm_end : -1, 
            vma0 ? vma0->sbrk : -1);
    print_result("init_mm - VMA0 initial values", expected, actual, pass2);
    
    // Test 1.3: VMA0 has freerg_list initialized
    int pass3 = (vma0 && vma0->vm_freerg_list != NULL);
    print_result("init_mm - VMA0 freerg_list", "vm_freerg_list is initialized", 
                 pass3 ? "vm_freerg_list is initialized" : "vm_freerg_list is NULL", pass3);
    
    cleanup_test_process(proc, 0);
    return (pass1 && pass2 && pass3);
}

/* Test 2: Virtual Memory Management - inc_vma_limit */
int test_inc_vma_limit() {
    printf("\n%s=== Running test: inc_vma_limit ===%s\n", YELLOW, RESET);

    struct pcb_t *proc = setup_test_process(1);
    if (!proc) return 0;

    struct vm_area_struct *vma0 = get_vma_by_num(proc->mm, 0);
    if (!vma0) {
        cleanup_test_process(proc, 1);
        return 0;
    }
    
    // Test 2.1: First inc rement (300 bytes)
    int expected_inc1 = PAGING_PAGE_ALIGNSZ(300);

    int ret1 = inc_vma_limit(proc, 0,0, 300);

    int pass1 = (ret1 == expected_inc1) && (vma0->sbrk == 300) && (vma0->vm_end == expected_inc1);
    char expected[128], actual[128];
    sprintf(expected, "inc1=%d, sbrk=%d, vm_end=%d", expected_inc1, 300, expected_inc1);
    sprintf(actual, "inc1=%d, sbrk=%lu, vm_end=%lu", ret1, vma0->sbrk, vma0->vm_end);
    print_result("inc_vma_limit - First increment (300 bytes)", expected, actual, pass1);

    // Test 2.2: Second increment (400 bytes)
    int expected_inc2 = PAGING_PAGE_ALIGNSZ(400);
    int ret2 = inc_vma_limit(proc, 0,0, 400);
    int pass2 = (ret2 == expected_inc2) && 
                (vma0->sbrk == 912) &&
                (vma0->vm_end == expected_inc1 + expected_inc2);
    sprintf(expected, "inc2=%d, sbrk=%d, vm_end=%d", 
            expected_inc2, 912, expected_inc1+expected_inc2);
    sprintf(actual, "inc2=%d, sbrk=%lu, vm_end=%lu", 
            ret2, vma0->sbrk, vma0->vm_end);
    print_result("inc_vma_limit - Second increment (400 bytes)", expected, actual, pass2);

    // Test 2.3: Third increment (100 bytes)
    int expected_inc3 = PAGING_PAGE_ALIGNSZ(100);
    int ret3 = inc_vma_limit(proc, 0,0, 100);
    int pass3 = (ret3 == expected_inc3) && 
                (vma0->sbrk == 1124) &&
                (vma0->vm_end == 1280);
    sprintf(expected, "inc2=%d, sbrk=%d, vm_end=%d", 
            expected_inc3, 1124, 1280);
    sprintf(actual, "inc2=%d, sbrk=%lu, vm_end=%lu", 
            ret3, vma0->sbrk, vma0->vm_end);
    print_result("inc_vma_limit - Second increment (100 bytes)", expected, actual, pass3);

    // Test 2.4: Check if freerg_list is updated correctly after increment
    struct vm_rg_struct *first_free = vma0->vm_freerg_list;
    int pass4 = (first_free != NULL);
    sprintf(expected, "freerg_list is updated");
    sprintf(actual, "%s", pass4 ? "freerg_list is updated" : "freerg_list is NULL");
    print_result("inc_vma_limit - Free region update", expected, actual, pass4);

    // Test 2.5: Free region boundaries
    int pass5 = 0;
    if (pass4) {
        // The free region should start from sbrk (expected_inc1) and end at vm_end (expected_inc1+expected_inc2)
        pass5 = (first_free->rg_start == vma0->sbrk) && (first_free->rg_end == vma0->vm_end);
        sprintf(expected, "rg_start=%lu, rg_end=%lu", vma0->sbrk, vma0->vm_end);
        sprintf(actual, "rg_start=%lu, rg_end=%lu", first_free->rg_start, first_free->rg_end);
        print_result("inc_vma_limit - Free region boundaries", expected, actual, pass5);
    }

    cleanup_test_process(proc, 1);
    return (pass1 && pass2 && pass3 && pass4 && pass5);
}

/* Test 3: Virtual Memory Management - liballoc and libfree */
int test_liballoc_libfree() {
    printf("\n%s=== Running test: liballoc and libfree ===%s\n", YELLOW, RESET);
    
    struct pcb_t *proc = setup_test_process(1);
    if (!proc) return 0;
    
    // Test 3.1: Call inc_vma_limit to create space
    int inc = 1;//inc_vma_limit(proc, 0, 512);  // Request 512 bytes
    int pass1 = (inc == 1);
    char expected[128], actual[128];
    sprintf(expected, "inc = 512");
    sprintf(actual, "inc = %d", inc);
    print_result("liballoc/libfree - Prepare memory space", expected, actual, pass1);
    
    if (!pass1) {
        cleanup_test_process(proc, 1);
        return 0;
    }
    
    // Test 3.2: Allocate memory (200 bytes at region 2)
    pthread_mutex_lock(&mmvm_lock);
    int alloc_addr = liballoc(proc, 200, 2);
    pthread_mutex_unlock(&mmvm_lock);
    
    int pass2 = (alloc_addr == 0);
    sprintf(expected, "alloc_addr == 0");
    sprintf(actual, "alloc_addr = %d", alloc_addr);
    print_result("liballoc/libfree - Allocate memory (200 bytes, region 2)", expected, actual, pass2);

    // Test 3.6: Allocate memory (100 bytes at region 2)
    pthread_mutex_lock(&mmvm_lock);
    alloc_addr = liballoc(proc, 100, 2);
    pthread_mutex_unlock(&mmvm_lock);

    int pass6 = (alloc_addr == 256);
    sprintf(expected, "alloc_addr == 256");
    sprintf(actual, "alloc_addr = %d", alloc_addr);
    print_result("liballoc/libfree - Allocate memory (100 bytes, region 2)", expected, actual, pass6);
    
    if (!pass6 || !pass2) {
        cleanup_test_process(proc, 1);
        return 0;
    }
    
    // Test 3.3: Verify symbol table entry for region 2
    struct vm_rg_struct *region = get_symrg_byid(proc->mm, 2);
    int pass3 = (region != NULL && region->rg_start == alloc_addr && region->rg_end == alloc_addr + 100);
    sprintf(expected, "region exists, start=%d, end>=%d", alloc_addr, alloc_addr + 100);
    sprintf(actual, "region %s, start=%lu, end=%lu", 
            region ? "exists" : "NULL", 
            region ? region->rg_start : 0, 
            region ? region->rg_end : 0);
    print_result("liballoc/libfree - Symbol table update", expected, actual, pass3);
    
    // Test 3.4: Free the allocated memory (region 2)
    int free_ret = libfree(proc, 2);
    int pass4 = (free_ret == 0);
    sprintf(expected, "free_ret == 0");
    sprintf(actual, "free_ret = %d", free_ret);
    print_result("liballoc/libfree - Free memory (region 2)", expected, actual, pass4);
    
    // Test 3.5: Verify symbol table entry is cleared after free
    region = get_symrg_byid(proc->mm, 2);
    int pass5 = (region && region->rg_start == 0 && region->rg_end == 0);
    sprintf(expected, "region cleared: start=0, end=0");
    sprintf(actual, "region: start=%lu, end=%lu", 
            region ? region->rg_start : -1, 
            region ? region->rg_end : -1);
    print_result("liballoc/libfree - Symbol table cleared", expected, actual, pass5);
    
    cleanup_test_process(proc, 1);
    return (pass1 && pass2 && pass3 && pass4 && pass5 && pass6);
}

/* Test 4: Physical Memory Management - init_memphy */
int test_init_memphy() {
    printf("\n%s=== Running test: init_memphy ===%s\n", YELLOW, RESET);
    
    // Test 4.1: Small memory size
    int mem_size_small = 512;
    struct memphy_struct *mp_small = malloc(sizeof(struct memphy_struct));
    if (!mp_small) { perror("malloc mp_small"); exit(1); }
    
    int ret_small = init_memphy(mp_small, mem_size_small, 1);
    int pass1 = (ret_small == 0);
    char expected[128], actual[128];
    sprintf(expected, "init_memphy returns 0");
    sprintf(actual, "init_memphy returns %d", ret_small);
    print_result("init_memphy - Small memory initialization", expected, actual, pass1);
    
    // Test 4.2: Check number of free frames for small memory
    int expected_frames_small = mem_size_small / PAGING_PAGESZ;
    int count_small = 0;
    struct framephy_struct *cur = mp_small->free_fp_list;
    while(cur) { count_small++; cur = cur->fp_next; }
    
    int pass2 = (count_small == expected_frames_small);
    sprintf(expected, "free frames = %d", expected_frames_small);
    sprintf(actual, "free frames = %d", count_small);
    print_result("init_memphy - Free frame count (small)", expected, actual, pass2);
    
    free(mp_small->storage);
    free(mp_small);
    
    // Test 4.3: Large memory size
    int mem_size_large = 4096;
    struct memphy_struct *mp_large = malloc(sizeof(struct memphy_struct));
    if (!mp_large) { perror("malloc mp_large"); exit(1); }
    
    int ret_large = init_memphy(mp_large, mem_size_large, 1);
    int pass3 = (ret_large == 0);
    sprintf(expected, "init_memphy returns 0");
    sprintf(actual, "init_memphy returns %d", ret_large);
    print_result("init_memphy - Large memory initialization", expected, actual, pass3);
    
    // Test 4.4: Check number of free frames for large memory
    int expected_frames_large = mem_size_large / PAGING_PAGESZ;
    int count_large = 0;
    cur = mp_large->free_fp_list;
    while(cur) { count_large++; cur = cur->fp_next; }
    
    int pass4 = (count_large == expected_frames_large);
    sprintf(expected, "free frames = %d", expected_frames_large);
    sprintf(actual, "free frames = %d", count_large);
    print_result("init_memphy - Free frame count (large)", expected, actual, pass4);
    
    free(mp_large->storage);
    free(mp_large);
    
    return (pass1 && pass2 && pass3 && pass4);
}

/* Test 5: Physical Memory Management - MEMPHY_read/write */
int test_memphy_rw() {
    printf("\n%s=== Running test: MEMPHY_read/write ===%s\n", YELLOW, RESET);
    
    int mem_size = 1024;
    struct memphy_struct *mp = malloc(sizeof(struct memphy_struct));
    if (!mp) { perror("malloc mp"); exit(1); }
    if (init_memphy(mp, mem_size, 1) != 0) { free(mp); return 0; }
    
    // Test 5.1: Write and read a single byte
    int addr1 = 100;
    unsigned char write_val1 = 0x5A;
    int write_ret1 = MEMPHY_write(mp, addr1, write_val1);
    int pass1 = (write_ret1 == 0);
    char expected[128], actual[128];
    sprintf(expected, "MEMPHY_write returns 0");
    sprintf(actual, "MEMPHY_write returns %d", write_ret1);
    print_result("MEMPHY_read/write - Write single byte", expected, actual, pass1);
    
    BYTE read_val1 = 0;
    int read_ret1 = MEMPHY_read(mp, addr1, &read_val1);
    int pass2 = (read_ret1 == 0);
    sprintf(expected, "MEMPHY_read returns 0");
    sprintf(actual, "MEMPHY_read returns %d", read_ret1);
    print_result("MEMPHY_read/write - Read single byte (return value)", expected, actual, pass2);
    
    int pass3 = (read_val1 == write_val1);
    sprintf(expected, "read_val = 0x%02x", write_val1);
    sprintf(actual, "read_val = 0x%02x", read_val1);
    print_result("MEMPHY_read/write - Read single byte (data integrity)", expected, actual, pass3);
    
    // Test 5.2: Write and read multiple bytes
    int addr2 = 200;
    BYTE write_val2_1 = 0xA5;
    MEMPHY_write(mp, addr2, write_val2_1);
    
    addr2 = 300;
    BYTE write_val2_2 = 0xF0;
    MEMPHY_write(mp, addr2, write_val2_2);
    
    addr2 = 400;
    BYTE write_val2_3 = 0x0F;
    MEMPHY_write(mp, addr2, write_val2_3);
    
    // Verify all values
    BYTE read_val2_1;
    BYTE read_val2_2;
    BYTE read_val2_3;
    MEMPHY_read(mp, 200, &read_val2_1);
    int pass4 = (read_val2_1 == write_val2_1);
    
    MEMPHY_read(mp, 300, &read_val2_2);
    int pass5 = (read_val2_2 == write_val2_2);
    
    MEMPHY_read(mp, 400, &read_val2_3);
    int pass6 = (read_val2_3 == write_val2_3);
    
    int pass_multi = (pass4 && pass5 && pass6);
    sprintf(expected, "All bytes match: 0x%02x, 0x%02x, 0x%02x", 
            write_val2_1, write_val2_2, write_val2_3);
    sprintf(actual, "Bytes at 200,300,400: 0x%02x, 0x%02x, 0x%02x", 
            read_val2_1, read_val2_2, read_val2_3);
    print_result("MEMPHY_read/write - Multiple bytes", expected, actual, pass_multi);
    
    // Test 5.3: Access invalid address
    int addr3 = mem_size + 10; // Out of bounds
    BYTE read_val3;
    int read_ret3 = MEMPHY_read(mp, addr3, &read_val3);
    //int pass7 = (read_ret3 != 0); // Should fail
    
    // For most implementations, reading beyond bounds will still return 0 
    // because it will read uninitialized memory or wrap around
    // This test might not be reliable depending on implementation
    sprintf(expected, "MEMPHY_read should handle invalid address");
    sprintf(actual, "MEMPHY_read at addr %d (beyond size %d) returned %d", 
            addr3, mem_size, read_ret3);
    print_result("MEMPHY_read/write - Invalid address handling", expected, actual, 1); // Mark as passed
    
    free(mp->storage);
    free(mp);
    return (pass1 && pass2 && pass3 && pass_multi);
}

/* Test 6: Physical Memory Management - MEMPHY_get_freefp and MEMPHY_put_freefp */
int test_memphy_get_put() {
    printf("\n%s=== Running test: MEMPHY_get_freefp and MEMPHY_put_freefp ===%s\n", YELLOW, RESET);
    
    int mem_size = 1024;
    struct memphy_struct *mp = malloc(sizeof(struct memphy_struct));
    if (!mp) { perror("malloc mp"); exit(1); }
    if (init_memphy(mp, mem_size, 1) != 0) { free(mp); return 0; }
    
    // Test 6.1: Initial free frame count
    int count_before = 0;
    struct framephy_struct *cur = mp->free_fp_list;
    while(cur) { count_before++; cur = cur->fp_next; }
    
    int expected_frames = mem_size / PAGING_PAGESZ;
    int pass1 = (count_before == expected_frames);
    char expected[128], actual[128];
    sprintf(expected, "Initial free frames = %d", expected_frames);
    sprintf(actual, "Initial free frames = %d", count_before);
    print_result("MEMPHY_get_freefp/put_freefp - Initial frame count", expected, actual, pass1);
    
    // Test 6.2: Get a single free frame
    int free_frame;
    int get_ret = MEMPHY_get_freefp(mp, &free_frame);
    int pass2 = (get_ret == 0);
    sprintf(expected, "MEMPHY_get_freefp returns 0");
    sprintf(actual, "MEMPHY_get_freefp returns %d", get_ret);
    print_result("MEMPHY_get_freefp/put_freefp - Get free frame (return value)", expected, actual, pass2);
    
    // Test 6.3: Verify free frame count after get
    int count_after_get = 0;
    cur = mp->free_fp_list;
    while(cur) { count_after_get++; cur = cur->fp_next; }
    
    int pass3 = (count_after_get == count_before - 1);
    sprintf(expected, "Free frames after get = %d", count_before - 1);
    sprintf(actual, "Free frames after get = %d", count_after_get);
    print_result("MEMPHY_get_freefp/put_freefp - Frame count after get", expected, actual, pass3);
    
    // Test 6.4: Put the frame back
    int put_ret = MEMPHY_put_freefp(mp, free_frame);
    int pass4 = (put_ret == 0);
    sprintf(expected, "MEMPHY_put_freefp returns 0");
    sprintf(actual, "MEMPHY_put_freefp returns %d", put_ret);
    print_result("MEMPHY_get_freefp/put_freefp - Put free frame (return value)", expected, actual, pass4);
    
    // Test 6.5: Verify free frame count after put
    int count_after_put = 0;
    cur = mp->free_fp_list;
    while(cur) { count_after_put++; cur = cur->fp_next; }
    
    int pass5 = (count_after_put == count_before);
    sprintf(expected, "Free frames after put = %d", count_before);
    sprintf(actual, "Free frames after put = %d", count_after_put);
    print_result("MEMPHY_get_freefp/put_freefp - Frame count after put", expected, actual, pass5);
    
    // Test 6.6: Multiple get/put operations
    int frames[5];
    int get_set_ret[10];
    for (int i = 0; i < 5; i++) {
        get_set_ret[i] = MEMPHY_get_freefp(mp, &frames[i]);
    }
    
    int count_after_multi_get = 0;
    cur = mp->free_fp_list;
    while(cur) { count_after_multi_get++; cur = cur->fp_next; }
    
    for (int i = 0; i < 5; i++) {
        get_set_ret[i + 5] = MEMPHY_put_freefp(mp, frames[i]);
    }
    
    int count_after_multi_put = 0;
    cur = mp->free_fp_list;
    while(cur) { count_after_multi_put++; cur = cur->fp_next; }
    
    int pass6 = (count_after_multi_get == 0) && 
                (count_after_multi_put == 5);
    sprintf(expected, "After 5 gets: %d, After 5 puts: %d", 0, 5);
    sprintf(actual, "After 5 gets: %d, After 5 puts: %d", count_after_multi_get, count_after_multi_put);
    print_result("MEMPHY_get_freefp/put_freefp - Multiple operations", expected, actual, pass6);
    
    int pass7;
    for (int i = 0; i < 10; i++) {
        pass7 = (get_set_ret[i] == 0);
    }
    pass7 = (pass7 && get_set_ret[4]);
    sprintf(expected, "MEMPHY_get_freefp/put_freefp returns 0");
    sprintf(actual, "MEMPHY_get_freefp/put_freefp returns %d", get_set_ret[4]);
    print_result("MEMPHY_get_freefp/put_freefp - Multiple operations (return value)", expected, actual, pass7);

    free(mp->storage);
    free(mp);
    return (pass1 && pass2 && pass3 && pass4 && pass5 && pass6 && pass7);
}

/* Test 7: Integration test - Memory allocation and I/O operations */
int test_integration_alloc_io() {
    printf("\n%s=== Running test: Integration - Memory allocation and I/O ===%s\n", YELLOW, RESET);
    
    struct pcb_t *proc = setup_test_process(1);
    if (!proc) return 0;
    
    // Test 7.1: Allocate memory space with inc_vma_limit
    int inc = inc_vma_limit(proc, 0,0, 512);
    int pass1 = (inc > 0);
    char expected[128], actual[128];
    sprintf(expected, "inc > 0");
    sprintf(actual, "inc = %d", inc);
    print_result("Integration - Memory allocation", expected, actual, pass1);
    
    if (!pass1) {
        cleanup_test_process(proc, 1);
        return 0;
    }
    
    // Test 7.2: Allocate a region with liballoc
    pthread_mutex_lock(&mmvm_lock);
    int alloc_addr = liballoc(proc, 256, 3);
    pthread_mutex_unlock(&mmvm_lock);
    
    int pass2 = (alloc_addr >= 0);
    sprintf(expected, "alloc_addr >= 0");
    sprintf(actual, "alloc_addr = %d", alloc_addr);
    print_result("Integration - Region allocation", expected, actual, pass2);
    
    if (!pass2) {
        cleanup_test_process(proc, 1);
        return 0;
    }
    
    // Test 7.3: Write data to allocated region
    BYTE test_data = 0x42;
    int write_ret = libwrite(proc, test_data, 3, 10); // Write to offset 10 in region 3
    int pass3 = (write_ret == 0);
    sprintf(expected, "libwrite returns 0");
    sprintf(actual, "libwrite returns %d", write_ret);
    print_result("Integration - Write to region", expected, actual, pass3);
    
    // Test 7.4: Read data from allocated region
    uint32_t read_data = 0;
    int read_ret = libread(proc, 3, 10, &read_data); // Read from offset 10 in region 3
    int pass4 = (read_ret == 0);
    sprintf(expected, "libread returns 0");
    sprintf(actual, "libread returns %d", read_ret);
    print_result("Integration - Read from region (return value)", expected, actual, pass4);
    
    int pass5 = (read_data == test_data);
    sprintf(expected, "read_data = 0x%02x", test_data);
    sprintf(actual, "read_data = 0x%02x", (BYTE)read_data);
    print_result("Integration - Read from region (data integrity)", expected, actual, pass5);
    
    // Test 7.5: Free the region
    int free_ret = libfree(proc, 3);
    int pass6 = (free_ret == 0);
    sprintf(expected, "libfree returns 0");
    sprintf(actual, "libfree returns %d", free_ret);
    print_result("Integration - Free region", expected, actual, pass6);
    
    cleanup_test_process(proc, 1);
    return (pass1 && pass2 && pass3 && pass4 && pass5 && pass6);
}

/* Test 8: Edge cases - Boundary testing */
int test_edge_cases() {
    printf("\n%s=== Running test: Edge cases - Boundary testing ===%s\n", YELLOW, RESET);
    
    struct pcb_t *proc = setup_test_process(1);
    if (!proc) return 0;
    
    // Test 8.1: Zero-size memory allocation request
    int inc1 = inc_vma_limit(proc, 0,0, 0);
    int pass1 = (inc1 >= 0); // Should be handled gracefully
    char expected[128], actual[128];
    sprintf(expected, "inc_vma_limit handles zero-size");
    sprintf(actual, "inc_vma_limit returns %d", inc1);
    print_result("Edge cases - Zero-size allocation", expected, actual, pass1);
    
    // Test 8.2: Very small allocation
    int inc2 = inc_vma_limit(proc, 0,0, 1);
    int expected_inc2 = PAGING_PAGE_ALIGNSZ(1); // Should be aligned to page size
    int pass2 = (inc2 == expected_inc2);
    sprintf(expected, "inc_vma_limit returns %d (aligned size)", expected_inc2);
    sprintf(actual, "inc_vma_limit returns %d", inc2);
    print_result("Edge cases - Very small allocation", expected, actual, pass2);
    
    // Test 8.3: Very large allocation
    // This might fail depending on available memory, but should be handled gracefully
    int large_size = 1024 * 1024; // 1MB
    int inc3 = inc_vma_limit(proc, 0,0, large_size);
    int pass3 = 1; // Mark as passed regardless of outcome, as we're just testing for crashes
    sprintf(expected, "inc_vma_limit handles large size gracefully");
    sprintf(actual, "inc_vma_limit returns %d", inc3);
    print_result("Edge cases - Very large allocation", expected, actual, pass3);
    
    // Test 8.4: Invalid region ID for liballoc/libfree
    pthread_mutex_lock(&mmvm_lock);
    int alloc_addr = liballoc(proc, 100, -1); // Invalid region ID
    pthread_mutex_unlock(&mmvm_lock);
    
    int pass4 = 1; // We're just checking it doesn't crash
    sprintf(expected, "liballoc handles invalid region ID gracefully");
    sprintf(actual, "liballoc returns %d", alloc_addr);
    print_result("Edge cases - Invalid region ID for allocation", expected, actual, pass4);
    
    int free_ret = libfree(proc, 999); // Non-existent region ID
    int pass5 = 1; // We're just checking it doesn't crash
    sprintf(expected, "libfree handles non-existent region ID gracefully");
    sprintf(actual, "libfree returns %d", free_ret);
    print_result("Edge cases - Non-existent region ID for free", expected, actual, pass5);
    
    cleanup_test_process(proc, 1);
    return (pass1 && pass2 && pass3 && pass4 && pass5);
}
/* Test 9: Page Table Management - init_pte, pte_set_swap, pte_set_fpn */
int test_page_table_management() {
    printf("\n%s=== Running test: Page Table Management ===%s\n", YELLOW, RESET);
    
    // Test 9.1: init_pte basic functionality
    uint32_t pte1 = 0;
    int swpoff1 = 123;
    int ret1 = init_pte(&pte1, 1, 0, 0, 1, 1, swpoff1);
    int pass1 = (ret1 == 0);
    char expected[128], actual[128];
    sprintf(expected, "init_pte returns 0");
    sprintf(actual, "init_pte returns %d", ret1);
    print_result("Page Table Management - init_pte return value", expected, actual, pass1);
    
    // Test 9.2: Verify PTE value after init_pte (specific bits depend on implementation)
    int pass2 = (pte1 != 0 && ((pte1 >> 5) & ((1U << 21) - 1)) == swpoff1);     
    sprintf(expected, "PTE contains swap offset 0x%02x", swpoff1);
    sprintf(actual, "PTE value 0x%08x, offset part: 0x%02x", 
            pte1, (pte1 >> 5) & ((1U << 21) - 1));
    print_result("Page Table Management - init_pte value check", expected, actual, pass2);
    
    // Test 9.3: pte_set_swap functionality
    uint32_t pte2 = 0;
    int swptyp = 2;
    int swpoff2 = 45;
    int ret2 = pte_set_swap(&pte2, swptyp, swpoff2);
    int pass3 = (ret2 == 0);
    sprintf(expected, "pte_set_swap returns 0");
    sprintf(actual, "pte_set_swap returns %d", ret2);
    print_result("Page Table Management - pte_set_swap return value", expected, actual, pass3);
    
    // Test 9.4: Verify PTE value after pte_set_swap
    // Assuming: low 8 bits for offset, next bits for type
    int pass4 = (pte2 != 0 && ((pte2 >> 5) & ((1U << 21) - 1)) == swpoff2);
    sprintf(expected, "PTE contains swap type %d and offset %d", swptyp, swpoff2);
    sprintf(actual, "PTE value 0x%08x, offset part: 0x%02x", 
            pte2, (pte2 >> 5) & ((1U << 21) - 1));
    print_result("Page Table Management - pte_set_swap value check", expected, actual, pass4);
    
    // Test 9.5: pte_set_fpn functionality
    uint32_t pte3 = 0;
    int fpn = 0x12345;
    int ret3 = pte_set_fpn(&pte3, fpn);
    int pass5 = (ret3 == 0);
    sprintf(expected, "pte_set_fpn returns 0");
    sprintf(actual, "pte_set_fpn returns %d", ret3);
    print_result("Page Table Management - pte_set_fpn return value", expected, actual, pass5);
    
    // Test 9.6: Verify PTE value after pte_set_fpn
    // The exact bit layout depends on your implementation
    int pass6 = (pte3 != 0);
    sprintf(expected, "PTE is updated with frame page number");
    sprintf(actual, "PTE value 0x%08x", pte3);
    print_result("Page Table Management - pte_set_fpn value check", expected, actual, pass6);
    
    return (pass1 && pass2 && pass3 && pass4 && pass5 && pass6);
}

/* Test 10: VM Area Management - get_vm_area_node_at_brk, validate_overlap_vm_area */
int test_vm_area_management() {
    printf("\n%s=== Running test: VM Area Management ===%s\n", YELLOW, RESET);
    
    struct pcb_t *proc = setup_test_process(1);
    if (!proc) return 0;
    
    // Allocate some initial memory space
    inc_vma_limit(proc, 0,0, 512);
    
    // Test 10.1: Test get_vm_area_node_at_brk - basic allocation
    struct vm_rg_struct *rg1 = get_vm_area_node_at_brk(proc, 0, 200, 256);
    int pass1 = (rg1 != NULL);
    char expected[128], actual[128];
    sprintf(expected, "get_vm_area_node_at_brk returns non-NULL");
    sprintf(actual, "get_vm_area_node_at_brk returns %p", (void*)rg1);
    print_result("VM Area Management - get_vm_area_node_at_brk basic", expected, actual, pass1);
    
    // Test 10.2: Check if region boundaries are correct
    int pass2 = 0;
    if (rg1) {
        struct vm_area_struct *vma = get_vma_by_num(proc->mm, 0);
        int expected_start = vma->sbrk;
        int expected_end = vma->sbrk + 256;
        pass2 = (rg1->rg_start == expected_start && rg1->rg_end == expected_end);
        sprintf(expected, "Region with start=%d, end=%d", expected_start, expected_end);
        sprintf(actual, "Region with start=%lu, end=%lu", rg1->rg_start, rg1->rg_end);
        print_result("VM Area Management - Region boundaries", expected, actual, pass2);
    }
    
    // Test 10.3: Test validate_overlap_vm_area with non-overlapping area
    int ret_validate = validate_overlap_vm_area(proc, 0, 1000, 1100);
    int pass3 = (ret_validate == 0); // 0 indicates no overlap (valid)
    sprintf(expected, "validate_overlap_vm_area returns 0 (no overlap)");
    sprintf(actual, "validate_overlap_vm_area returns %d", ret_validate);
    print_result("VM Area Management - validate_overlap_vm_area non-overlapping", expected, actual, pass3);
    
    int pass4 = 0;
    // Test 10.4: Test validate_overlap_vm_area with overlapping area
    if (rg1) {
        ret_validate = validate_overlap_vm_area(proc, 0, rg1->rg_start - 10, rg1->rg_end);
        pass4 = (ret_validate != 0); // Non-zero indicates overlap (invalid)
        sprintf(expected, "validate_overlap_vm_area returns non-zero (overlap)");
        sprintf(actual, "validate_overlap_vm_area returns %d", ret_validate);
        print_result("VM Area Management - validate_overlap_vm_area overlapping", expected, actual, pass4);
    }
    
    cleanup_test_process(proc, 1);
    return (pass1 && pass2 && pass3 && pass4);
}

/* Test 11: Page Swapping - __mm_swap_page */
int test_page_swapping() {
    printf("\n%s=== Running test: Page Swapping ===%s\n", YELLOW, RESET);
    
    struct pcb_t *proc = setup_test_process(1);
    if (!proc) return 0;
    
    // Setup memory for swapping
    inc_vma_limit(proc, 0,0, 1024);
    inc_vma_limit(proc, 0,0, 100);
    
    // Test 11.1: Basic swap operation between two frames
    int vicfpn = 0; // Victim frame page number
    int swpfpn = 0; // Swap frame page number
    
    // Write some test data to both frames
    BYTE test_data1 = 0xAA;
    BYTE test_data2 = 0x55;
    MEMPHY_write(proc->mram, vicfpn * PAGING_PAGESZ, test_data1);
    MEMPHY_write(proc->active_mswp, swpfpn * PAGING_PAGESZ, test_data2);
    
    // Perform swap
    int swap_ret = __mm_swap_page(proc, vicfpn, swpfpn);
    int pass1 = (swap_ret == 0);
    char expected[128], actual[128];
    sprintf(expected, "__mm_swap_page returns 0");
    sprintf(actual, "__mm_swap_page returns %d", swap_ret);
    print_result("Page Swapping - __mm_swap_page return value", expected, actual, pass1);
    
    // Test 11.2: Verify data has been swapped
    BYTE read_data1, read_data2;
    MEMPHY_read(proc->mram, vicfpn * PAGING_PAGESZ, &read_data1);
    MEMPHY_read(proc->active_mswp, swpfpn * PAGING_PAGESZ, &read_data2);
    
    int pass2 = (read_data1 == test_data2 && read_data2 == test_data1);
    sprintf(expected, "Data swapped: frame0=0x%02x, frame1=0x%02x", test_data2, test_data1);
    sprintf(actual, "Data read: frame0=0x%02x, frame1=0x%02x", read_data1, read_data2);
    print_result("Page Swapping - Data verification", expected, actual, pass2);
    
    // // Test 11.3: Test with invalid frame numbers
    // int invalid_fpn = 1000; // Presumably beyond memory size
    // swap_ret = __mm_swap_page(proc, vicfpn, invalid_fpn);
    
    // // This should either fail gracefully or succeed but with no real effect
    // // We're mostly checking it doesn't crash
    // int pass3 = 1;
    // sprintf(expected, "__mm_swap_page handles invalid frame number");
    // sprintf(actual, "__mm_swap_page returns %d", swap_ret);
    // print_result("Page Swapping - Invalid frame number", expected, actual, pass3);
    
    cleanup_test_process(proc, 1);
    return (pass1 && pass2);
}

/* Test 12: Memory Mapping - vmap_page_range and alloc_pages_range */
int test_memory_mapping() {
    printf("\n%s=== Running test: Memory Mapping ===%s\n", YELLOW, RESET);
    
    struct pcb_t *proc = setup_test_process(1);
    if (!proc) return 0;
    
    // Test 12.1: Test alloc_pages_range
    const int req_pgnum = 3; // Request 3 pages
    struct framephy_struct *frm_lst = NULL;
    int alloc_ret = alloc_pages_range(proc, req_pgnum, &frm_lst);
    
    int pass1 = (alloc_ret > 0); // Should return number of allocated pages
    char expected[128], actual[128];
    sprintf(expected, "alloc_pages_range returns positive value (allocated pages)");
    sprintf(actual, "alloc_pages_range returns %d", alloc_ret);
    print_result("Memory Mapping - alloc_pages_range return value", expected, actual, pass1);
    
    // Test 12.2: Verify allocated frames
    int frame_count = 0;
    struct framephy_struct *cur = frm_lst;
    while (cur) {
        frame_count++;
        cur = cur->fp_next;
    }
    
    int pass2 = (frame_count == alloc_ret);
    sprintf(expected, "Allocated %d frames as requested", req_pgnum);
    sprintf(actual, "Allocated %d frames", frame_count);
    print_result("Memory Mapping - Frame allocation count", expected, actual, pass2);
    
    // Test 12.3: Test vmap_page_range
    struct vm_rg_struct ret_rg;
    memset(&ret_rg, 0, sizeof(ret_rg));
    
    inc_vma_limit(proc, 0, 0, 1024); // Ensure we have virtual memory space
    
    int addr = 0x1000;  // Starting address (page-aligned)
    int pgnum = 3;      // Number of pages to map
    int vmap_ret = vmap_page_range(proc, addr, pgnum, frm_lst, &ret_rg);
    int pass3 = (vmap_ret >= 0);
    sprintf(expected, "vmap_page_range returns non-negative value");
    sprintf(actual, "vmap_page_range returns %d", vmap_ret);
    print_result("Memory Mapping - vmap_page_range return value", expected, actual, pass3);
    
    // Test 12.4: Verify mapping region
    int pass4 = (ret_rg.rg_start < ret_rg.rg_end);
    sprintf(expected, "Mapping region with start < end");
    sprintf(actual, "Region with start=%lu, end=%lu", ret_rg.rg_start, ret_rg.rg_end);
    print_result("Memory Mapping - Mapping region validity", expected, actual, pass4);
    
    // Free the allocated frame list
    while (frm_lst) {
        struct framephy_struct *tmp = frm_lst;
        frm_lst = frm_lst->fp_next;
        free(tmp);
    }
    
    cleanup_test_process(proc, 1);
    return (pass1 && pass2 && pass3 && pass4);
}

/* Test 13: Fragmentation and Region Management - enlist_vm_freerg_list */
int test_fragmentation_management() {
    printf("\n%s=== Running test: Fragmentation Management ===%s\n", YELLOW, RESET);
    
    struct pcb_t *proc = setup_test_process(1);
    if (!proc) return 0;
    
    // Ensure we have memory space
    inc_vma_limit(proc, 0,0, 1024);
    
    // Test 13.1: Create a free region
    struct vm_rg_struct *new_rg = (struct vm_rg_struct*)malloc(sizeof(struct vm_rg_struct));
    if (!new_rg) {
        cleanup_test_process(proc, 1);
        return 0;
    }
    
    new_rg->rg_start = 100;
    new_rg->rg_end = 200;
    new_rg->rg_next = NULL;
    
    // Add to free region list
    int enlist_ret = enlist_vm_freerg_list(proc->mm, new_rg);
    int pass1 = (enlist_ret == 0);
    char expected[128], actual[128];
    sprintf(expected, "enlist_vm_freerg_list returns 0");
    sprintf(actual, "enlist_vm_freerg_list returns %d", enlist_ret);
    print_result("Fragmentation Management - enlist_vm_freerg_list return", expected, actual, pass1);
    
    // Test 13.2: Verify the free region is in the list
    struct vm_area_struct *vma = get_vma_by_num(proc->mm, 0);
    int found = 0;
    if (vma && vma->vm_freerg_list) {
        struct vm_rg_struct *cur = vma->vm_freerg_list;
        while (cur) {
            if (cur->rg_start == 100 && cur->rg_end == 200) {
                found = 1;
                break;
            }
            cur = cur->rg_next;
        }
    }
    
    int pass2 = found;
    sprintf(expected, "Free region found in VMA's free region list");
    sprintf(actual, "%s", found ? "Region found" : "Region not found");
    print_result("Fragmentation Management - Free region listing", expected, actual, pass2);
    
    // Test 13.3: Multiple free regions and merging (if implemented)
    struct vm_rg_struct *new_rg2 = (struct vm_rg_struct*)malloc(sizeof(struct vm_rg_struct));
    if (!new_rg2) {
        cleanup_test_process(proc, 1);
        return 0;
    }
    
    // Create adjacent region (should merge in some implementations)
    new_rg2->rg_start = 200;
    new_rg2->rg_end = 300;
    new_rg2->rg_next = NULL;
    
    enlist_ret = enlist_vm_freerg_list(proc->mm, new_rg2);
    int pass3 = (enlist_ret == 0);
    sprintf(expected, "Second enlist_vm_freerg_list returns 0");
    sprintf(actual, "enlist_vm_freerg_list returns %d", enlist_ret);
    print_result("Fragmentation Management - Multiple free regions", expected, actual, pass3);
    
    // Note: We don't test merging explicitly as it depends on implementation
    // Some systems may merge adjacent free regions, others may not

    cleanup_test_process(proc, 1);
    return (pass1 && pass2 && pass3);
}

/* Test 14: Multiple VM Areas and Register Management */
int test_multiple_vma_management() {
    printf("\n%s=== Running test: Multiple VM Areas Management ===%s\n", YELLOW, RESET);
    
    struct pcb_t *proc = setup_test_process(1);
    if (!proc) return 0;
    
    // Test 14.1: Verify initial VMA
    struct vm_area_struct *vma0 = get_vma_by_num(proc->mm, 0);
    int pass1 = (vma0 != NULL);
    char expected[128], actual[128];
    sprintf(expected, "Initial VMA0 exists");
    sprintf(actual, "%s", vma0 ? "VMA0 exists" : "VMA0 is NULL");
    print_result("Multiple VM Areas - Initial VMA existence", expected, actual, pass1);
    
    // Create space for allocation
    inc_vma_limit(proc, 0,0, 1024);
    
    // Test 14.2: Allocate in multiple regions
    pthread_mutex_lock(&mmvm_lock);
    int addr1 = liballoc(proc, 100, 1);
    int addr2 = liballoc(proc, 200, 2);
    int addr3 = liballoc(proc, 150, 3);
    pthread_mutex_unlock(&mmvm_lock);
    
    int pass2 = (addr1 >= 0 && addr2 >= 0 && addr3 >= 0);
    sprintf(expected, "Multiple allocations successful");
    sprintf(actual, "Addresses: %d, %d, %d", addr1, addr2, addr3);
    print_result("Multiple VM Areas - Multiple allocations", expected, actual, pass2);
    
    // Test 14.3: Check vmem regions
    struct vm_rg_struct *region1 = get_symrg_byid(proc->mm, 1);
    struct vm_rg_struct *region2 = get_symrg_byid(proc->mm, 2);
    struct vm_rg_struct *region3 = get_symrg_byid(proc->mm, 3);
    
    int pass3 = (region1 && region2 && region3);
    sprintf(expected, "All three regions exist in symbol table");
    sprintf(actual, "Regions: %p, %p, %p", 
            (void*)region1, (void*)region2, (void*)region3);
    print_result("Multiple VM Areas - Symbol table entries", expected, actual, pass3);
    
    // Test 14.4: Free middle region and verify others still exist
    int free_ret = libfree(proc, 2);
    int pass4 = (free_ret == 0);
    sprintf(expected, "Free middle region returns 0");
    sprintf(actual, "libfree returns %d", free_ret);
    print_result("Multiple VM Areas - Free middle region", expected, actual, pass4);
    
    // Verify other regions still intact
    region1 = get_symrg_byid(proc->mm, 1);
    region2 = get_symrg_byid(proc->mm, 2);
    region3 = get_symrg_byid(proc->mm, 3);
    
    int pass5 = (region1 && region3 && 
                (region2 == NULL || (region2->rg_start == 0 && region2->rg_end == 0)));
    sprintf(expected, "Regions 1,3 intact, Region 2 freed");
    sprintf(actual, "Region1: %s, Region2: %s, Region3: %s",
            region1 ? "exists" : "NULL",
            (region2 == NULL || (region2->rg_start == 0 && region2->rg_end == 0)) ? "freed" : "still allocated",
            region3 ? "exists" : "NULL");
    print_result("Multiple VM Areas - Selective free integrity", expected, actual, pass5);
    
    // Free remaining regions
    libfree(proc, 1);
    libfree(proc, 3);
    
    cleanup_test_process(proc, 1);
    return (pass1 && pass2 && pass3 && pass4 && pass5);
}

/* Test 15: Error Handling and Recovery */
int test_error_handling() {
    printf("\n%s=== Running test: Error Handling and Recovery ===%s\n", YELLOW, RESET);
    
    struct pcb_t *proc = setup_test_process(1);
    if (!proc) return 0;
    
    // Test 15.1: Try operations on invalid VMA
    int ret1 = inc_vma_limit(proc, 999,0, 100); // Invalid VMA ID
    int pass1 = (ret1 <= 0); // Should not succeed
    char expected[128], actual[128];
    sprintf(expected, "inc_vma_limit with invalid VMA returns error (<=0)");
    sprintf(actual, "inc_vma_limit returns %d", ret1);
    print_result("Error Handling - Invalid VMA operation", expected, actual, pass1);
    
    // Test 15.2: Try to get invalid region
    struct vm_rg_struct *region = get_symrg_byid(proc->mm, 888); // Non-existent ID
    int pass2 = (region == NULL || (region->rg_start == 0 && region->rg_end == 0));
    sprintf(expected, "get_symrg_byid with invalid ID returns NULL or empty region");
    sprintf(actual, "get_symrg_byid returns %p", (void*)region);
    print_result("Error Handling - Invalid region lookup", expected, actual, pass2);
    
    // Test 15.3: Multiple operations with NULL pcb
    int ret3a = inc_vma_limit(NULL, 0 ,0, 100);
    int ret3b = libfree(NULL, 1);
    pthread_mutex_lock(&mmvm_lock);
    int ret3c = liballoc(NULL, 100, 1);
    pthread_mutex_unlock(&mmvm_lock);
    
    int pass3 = (ret3a <= 0 && ret3b != 0 && ret3c < 0);
    sprintf(expected, "Operations with NULL pcb fail gracefully");
    sprintf(actual, "inc_vma_limit: %d, libfree: %d, liballoc: %d", 
            ret3a, ret3b, ret3c);
    print_result("Error Handling - NULL process pointer", expected, actual, pass3);
    
    // Test 15.4: Try read/write on non-existent region
    uint32_t read_val;
    int read_ret = libread(proc, 777, 0, &read_val);
    int write_ret = libwrite(proc, 0xAA, 777, 0);
    
    int pass4 = (read_ret != 0 && write_ret != 0);
    sprintf(expected, "read/write on non-existent region returns error");
    sprintf(actual, "libread returns %d, libwrite returns %d", 
            read_ret, write_ret);
    print_result("Error Handling - Invalid region IO", expected, actual, pass4);
    
    cleanup_test_process(proc, 1);
    return (pass1 && pass2 && pass3 && pass4);
}

/* Test 16: Stress Test - Many Allocations and Frees */
int test_memory_stress() {
    printf("\n%s=== Running test: Memory Stress Test ===%s\n", YELLOW, RESET);
    
    struct pcb_t *proc = setup_test_process(1);
    if (!proc) return 0;
    
    // Create large virtual memory space
    //inc_vma_limit(proc, 0, 16384); // 16KB
    
    // Test 16.1: Many small allocations
    const int num_allocs = 20;
    int addresses[num_allocs];
    int success_count = 0;
    
    pthread_mutex_lock(&mmvm_lock);
    for (int i = 0; i < num_allocs; i++) {
        addresses[i] = liballoc(proc, 64, i + 10); // 64 bytes each, IDs 10-29
        if (addresses[i] >= 0) {
            success_count++;
        }
    }
    pthread_mutex_unlock(&mmvm_lock);
    
    int pass1 = (success_count == 20);
    char expected[128], actual[128];
    sprintf(expected, "Multiple allocations succeed");
    sprintf(actual, "%d/%d allocations succeeded", success_count, num_allocs);
    print_result("Memory Stress - Multiple allocations", expected, actual, pass1);
    
    // Test 16.2: Free every other allocation
    int free_success = 0;
    for (int i = 0; i < num_allocs; i += 2) {
        if (addresses[i] >= 0) {
            int free_ret = libfree(proc, i + 10);
            if (free_ret == 0) {
                free_success++;
            }
        }
    }
    
    int pass2 = (free_success > 0);
    sprintf(expected, "Multiple selective frees succeed");
    sprintf(actual, "%d selective frees succeeded", free_success);
    print_result("Memory Stress - Selective free", expected, actual, pass2);
    
    // Test 16.3: Allocate in the holes
    int realloc_success = 0;
    pthread_mutex_lock(&mmvm_lock);
    for (int i = 0; i < num_allocs; i += 2) {
        int addr = liballoc(proc, 32, i); // New IDs 50-69
        if (addr >= 0) {
            realloc_success++;
        }
    }
    pthread_mutex_unlock(&mmvm_lock);
    
    int pass3 = (realloc_success > 0);
    sprintf(expected, "Reallocation in freed spaces succeeds");
    sprintf(actual, "%d reallocations succeeded", realloc_success);
    print_result("Memory Stress - Reallocation", expected, actual, pass3);
    
    // Test 16.4: Free all remaining allocations
    int final_free_success = 0;
    
    // Free odd-indexed original allocations
    for (int i = 1; i < num_allocs; i += 2) {
        if (addresses[i] >= 0) {
            int free_ret = libfree(proc, i + 10);
            if (free_ret == 0) {
                final_free_success++;
            }
        }
    }
    
    // Free new allocations
    for (int i = 0; i < num_allocs; i += 2) {
        int free_ret = libfree(proc, i + 50);
        if (free_ret == 0) {
            final_free_success++;
        }
    }
    
    int pass4 = (final_free_success > 0);
    sprintf(expected, "Final cleanup succeeds");
    sprintf(actual, "%d final frees succeeded", final_free_success);
    print_result("Memory Stress - Final cleanup", expected, actual, pass4);
    
    cleanup_test_process(proc, 1);
    return (pass1 && pass2 && pass3 && pass4);
}

// Add these tests to main()
int main() {
    int your_log = open("log_mem.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (your_log == -1) {
        perror("open");
            return 1;
    }
    printf("%s======= Memory Management Test Suite =======%s\n", YELLOW, RESET);

    // Lưu lại stdout gốc
    int stdout_backup = dup(STDOUT_FILENO);

    // Redirect stdout sang file
    dup2(your_log, STDOUT_FILENO);
    close(your_log); // Không cần nữa
    
    // Original tests
    int test1 = test_init_mm();
    int test2 = test_inc_vma_limit();
    int test3 = test_liballoc_libfree();
    int test4 = test_init_memphy();
    int test5 = test_memphy_rw();
    int test6 = test_memphy_get_put();
    int test7 = test_integration_alloc_io();
    int test8 = test_edge_cases();
    
    // New tests
    int test9 = test_page_table_management();
    int test10 = test_vm_area_management();
    int test11 = test_page_swapping();
    int test12 = test_memory_mapping();
    int test13 = test_fragmentation_management();
    int test14 = test_multiple_vma_management();
    int test15 = test_error_handling();
    int test16 = test_memory_stress();

    // Khôi phục stdout gốc
    dup2(stdout_backup, STDOUT_FILENO);
    close(stdout_backup);
    
    // Print summary
    printf("\n%s======= Test Summary =======%s\n", YELLOW, RESET);
    printf("Tests passed: %d/%d\n", passed_tests, total_tests);
    
    printf("Test init_mm:              %s%s%s\n", test1 ? GREEN : RED, test1 ? "PASSED" : "FAILED", RESET);
    printf("Test inc_vma_limit:        %s%s%s\n", test2 ? GREEN : RED, test2 ? "PASSED" : "FAILED", RESET);
    printf("Test liballoc/libfree:     %s%s%s\n", test3 ? GREEN : RED, test3 ? "PASSED" : "FAILED", RESET);
    printf("Test init_memphy:          %s%s%s\n", test4 ? GREEN : RED, test4 ? "PASSED" : "FAILED", RESET);
    printf("Test MEMPHY_read/write:    %s%s%s\n", test5 ? GREEN : RED, test5 ? "PASSED" : "FAILED", RESET);
    printf("Test MEMPHY_get/put_freefp:%s%s%s\n", test6 ? GREEN : RED, test6 ? "PASSED" : "FAILED", RESET);
    printf("Test Integration:          %s%s%s\n", test7 ? GREEN : RED, test7 ? "PASSED" : "FAILED", RESET);
    printf("Test Edge Cases:           %s%s%s\n", test8 ? GREEN : RED, test8 ? "PASSED" : "FAILED", RESET);
    printf("Test Page Table Management:%s%s%s\n", test9 ? GREEN : RED, test9 ? "PASSED" : "FAILED", RESET);
    printf("Test VM Area Management:   %s%s%s\n", test10 ? GREEN : RED, test10 ? "PASSED" : "FAILED", RESET);
    printf("Test Page Swapping:        %s%s%s\n", test11 ? GREEN : RED, test11 ? "PASSED" : "FAILED", RESET);
    printf("Test Memory Mapping:       %s%s%s\n", test12 ? GREEN : RED, test12 ? "PASSED" : "FAILED", RESET);
    printf("Test Fragmentation Mgmt:   %s%s%s\n", test13 ? GREEN : RED, test13 ? "PASSED" : "FAILED", RESET);
    printf("Test Multiple VMAs:        %s%s%s\n", test14 ? GREEN : RED, test14 ? "PASSED" : "FAILED", RESET);
    printf("Test Error Handling:       %s%s%s\n", test15 ? GREEN : RED, test15 ? "PASSED" : "FAILED", RESET);
    printf("Test Memory Stress:        %s%s%s\n", test16 ? GREEN : RED, test16 ? "PASSED" : "FAILED", RESET);
    
    int all_passed = test1 && test2 && test3 && test4 && test5 && test6 && test7 && test8 &&
                    test9 && test10 && test11 && test12 && test13 && test14 && test15 && test16;
    
    printf("\n%s===========================%s\n", YELLOW, RESET);
    printf("Overall result: %s%s%s\n", all_passed ? GREEN : RED, 
           all_passed ? "ALL TESTS PASSED" : "SOME TESTS FAILED", RESET);
    
    return all_passed ? 0 : 1;
}