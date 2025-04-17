/*
 * Copyright (C) 2025 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Sierra release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * System Library
 * Memory Module Library libmem.c
 */

 #include "string.h"
 #include "mm.h"
 #include "syscall.h"
 #include "libmem.h"
 #include <stdlib.h>
 #include <stdio.h>
 #include <pthread.h>
 
 static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;
 
 /*enlist_vm_freerg_list - add new rg to freerg_list
  *@mm: memory region
  *@rg_elmt: new region
  *
  */
 int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
 {
   struct vm_rg_struct *rg_new_node = malloc(sizeof(struct vm_rg_struct));
   if (!rg_new_node)
   {
     return -1;
   }
 
   struct vm_area_struct *cur_vma = get_vma_by_num(mm, rg_elmt->vmaid);
   if (!cur_vma)
   {
     free(rg_new_node);
     return -1;
   }
 
   int vmaid = rg_elmt->vmaid;
   int lower_bound = rg_elmt->rg_start;
   int upper_bound = rg_elmt->rg_end;
   int temp_lower_bound = (vmaid) ? (lower_bound + 1) : (lower_bound - 1);
   int temp_upper_bound = (vmaid) ? (upper_bound - 1) : (upper_bound + 1);
 
   // // Kiểm tra giá trị hợp lệ
   if (lower_bound >= upper_bound && rg_elmt->vmaid == 0)
   {
     free(rg_new_node);
     return -1;
   }
   if (lower_bound <= upper_bound && rg_elmt->vmaid == 1)
   {
     free(rg_new_node);
     return -1;
   }
 
   // Cập nhật thông tin vùng mới
   rg_new_node->rg_start = lower_bound;
   rg_new_node->rg_end = upper_bound;
   rg_new_node->rg_next = NULL;
 
   struct vm_rg_struct *prev = NULL;
 
   // Trường hợp danh sách trống hoặc vùng mới nằm trước tất cả
   if (vmaid)
   {
     if (!cur_vma->vm_freerg_list || cur_vma->vm_freerg_list->rg_start <= upper_bound)
     {
       rg_new_node->rg_next = cur_vma->vm_freerg_list;
       cur_vma->vm_freerg_list = rg_new_node;
     }
     else
     {
       // Duyệt danh sách để tìm vị trí phù hợp
       prev = cur_vma->vm_freerg_list;
       while (prev->rg_next && prev->rg_next->rg_start > upper_bound)
       {
         prev = prev->rg_next;
       }
       rg_new_node->rg_next = prev->rg_next;
       prev->rg_next = rg_new_node;
     }
   }
   else
   {
     if (!cur_vma->vm_freerg_list || cur_vma->vm_freerg_list->rg_start >= upper_bound)
     {
       rg_new_node->rg_next = cur_vma->vm_freerg_list;
       cur_vma->vm_freerg_list = rg_new_node;
     }
     else
     {
       // Duyệt danh sách để tìm vị trí phù hợp
       prev = cur_vma->vm_freerg_list;
       while (prev->rg_next && prev->rg_next->rg_start < upper_bound)
       {
         prev = prev->rg_next;
       }
       rg_new_node->rg_next = prev->rg_next;
       prev->rg_next = rg_new_node;
     }
   }
 
   // Merge với vùng liền kề phía trước (nếu có)
   if (prev && prev->rg_end == temp_lower_bound)
   {
     prev->rg_end = upper_bound;
     prev->rg_next = rg_new_node->rg_next;
     free(rg_new_node);
   }
 
   // Merge với vùng liền kề phía sau (nếu có)
   struct vm_rg_struct *deleted = prev->rg_next;
   if (prev->rg_next && temp_upper_bound == prev->rg_next->rg_start)
   {
     prev->rg_end = prev->rg_next->rg_end;
     prev->rg_next = deleted->rg_next;
     free(deleted);
   }
   return 0;
 }
 
 /*get_symrg_byid - get mem region by region ID
  *@mm: memory region
  *@rgid: region ID act as symbol index of variable
  *
  */
 struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
 {
   if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
     return NULL;
 
   if (mm->symrgtbl[rgid].rg_start == -1 || mm->symrgtbl[rgid].rg_end == -1)
   {
     printf("\tACCESS FREE REGION\n");
     return NULL;
   }
 
   return &mm->symrgtbl[rgid];
 }
 /*__alloc - allocate a region memory
  *@caller: caller
  *@vmaid: ID vm area to alloc memory region
  *@rgid: memory region ID (used to identify variable in symbole table)
  *@size: allocated size
  *@alloc_addr: address of allocated memory region
  *
  */
 int __alloc(struct pcb_t *caller, int vmaid, int rgid, int size, int *alloc_addr)
 {
   /*Allocate at the toproof */
   struct vm_rg_struct rgnode;
 
   /* TODO: commit the vmaid */
   // rgnode.vmaid
   if (rgid < 0){
    return -1;
   }
   if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
   {
     caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
     caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
 
     caller->mm->symrgtbl[rgid].vmaid = vmaid;
 
     *alloc_addr = rgnode.rg_start;
     printf("\tregister: %d; start: %ld; end: %ld\n", rgid, caller->mm->symrgtbl[rgid].rg_start, caller->mm->symrgtbl[rgid].rg_end);
     print_pgtbl(caller, 0, -1);
 
     return 0;
   }
 
   pthread_mutex_init(&mmvm_lock, NULL);
  
   struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
 
   int old_sbrk = cur_vma->sbrk;
 
   pthread_mutex_lock(&mmvm_lock);
   if (abs(cur_vma->vm_end - cur_vma->sbrk) < size)
   {
     if (inc_vma_limit(caller, vmaid, size) == -1)
     {
       pthread_mutex_unlock(&mmvm_lock);
       return -1;
     }
   }
   pthread_mutex_unlock(&mmvm_lock);
 
   if (vmaid == 0)
   {
     caller->mm->symrgtbl[rgid].rg_start = old_sbrk;
     caller->mm->symrgtbl[rgid].rg_end = old_sbrk + size - 1;
     caller->mm->symrgtbl[rgid].vmaid = vmaid;
     cur_vma->sbrk += size;
   }
   else
   {
     caller->mm->symrgtbl[rgid].rg_start = old_sbrk;
     caller->mm->symrgtbl[rgid].rg_end = old_sbrk - size + 1;
     caller->mm->symrgtbl[rgid].vmaid = vmaid;
     cur_vma->sbrk -= size;
   }
 
   printf("\tregister: %d; start: %ld; end: %ld\n", rgid, caller->mm->symrgtbl[rgid].rg_start, caller->mm->symrgtbl[rgid].rg_end);
   *alloc_addr = old_sbrk;
 
   // for debug
   print_pgtbl(caller, 0, -1);
   return 0;
 }
 
 /*__free - remove a region memory
  *@caller: caller
  *@vmaid: ID vm area to alloc memory region
  *@rgid: memory region ID (used to identify variable in symbole table)
  *@size: allocated size
  *
  */
 int __free(struct pcb_t *caller, int vmaid, int rgid)
 {
   // struct vm_rg_struct rgnode;
   pthread_mutex_lock(&mmvm_lock);
   struct vm_rg_struct *rgnode;
   // Dummy initialization for avoding compiler dummay warning
   // in incompleted TODO code rgnode will overwrite through implementing
   // the manipulation of rgid later
 
   if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
   {
     pthread_mutex_unlock(&mmvm_lock);
     return -1;
   }
   /* TODO: Manage the collect freed region to freerg_list */
   rgnode = &(caller->mm->symrgtbl[rgid]);
   if (rgnode->rg_start == -1 && rgnode->rg_end == -1)
   {
     pthread_mutex_unlock(&mmvm_lock);
     return -1;
   }
 
   /*enlist the obsoleted memory region */
   // enlist_vm_freerg_list();
 
   enlist_vm_freerg_list(caller->mm, rgnode); // clone node lại
   rgnode->rg_start = -1;
   rgnode->rg_end = -1;
   rgnode->rg_next = NULL;
   pthread_mutex_unlock(&mmvm_lock);
 
   print_pgtbl(caller, 0, -1);
 
   return 0;
 }
 
 /*liballoc - PAGING-based allocate a region memory
  *@proc:  Process executing the instruction
  *@size: allocated size
  *@reg_index: memory region ID (used to identify variable in symbole table)
  */
 int liballoc(struct pcb_t *proc, uint32_t size, uint32_t reg_index)
 {
   /* TODO Implement allocation on vm area 0 */
   int addr;
 
   /* By default using vmaid = 0 */
   return __alloc(proc, 0, reg_index, size, &addr);
 }
 
 /*libfree - PAGING-based free a region memory
  *@proc: Process executing the instruction
  *@size: allocated size
  *@reg_index: memory region ID (used to identify variable in symbole table)
  */
 
 int libfree(struct pcb_t *proc, uint32_t reg_index)
 {
   /* TODO Implement free region */
 
   /* By default using vmaid = 0 */
   return __free(proc, 0, reg_index);
 }
 
 /*pg_getpage - get the page in ram
  *@mm: memory region
  *@pagenum: PGN
  *@framenum: return FPN
  *@caller: caller
  *
  */
 int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
 {
   uint32_t pte = mm->pgd[pgn];
 
   if (!PAGING_PAGE_PRESENT(pte))
   { /* Page is not online, make it actively living */
     int vicpgn, swpfpn;
     int vicfpn;
     uint32_t vicpte;
 
     int tgtfpn = PAGING_PTE_SWP(pte); // The target frame storing our variable in swap
 
     /* Find victim page */
     find_victim_page(caller->mm, &vicpgn);
 
     /* Get free frame in MEMSWP */
     MEMPHY_get_freefp(caller->active_mswp, &swpfpn);
 
     /* Get victim frame in MEMRAM */
     vicpte = caller->mm->pgd[vicpgn];
     vicfpn = PAGING_FPN(vicpte);
 
     /* Copy victim frame to swap */
     struct sc_regs regs;
     regs.a1 = SYSMEM_SWP_OP; // Operation: Swap memory
     regs.a2 = vicfpn;        // Source frame (victim frame in RAM)
     regs.a3 = swpfpn;        // Destination frame (free frame in swap)
 
     /* SYSCALL 17 sys_memmap to swap victim to swap space */
     syscall(caller, 17, &regs);
 
     /* Update page table for victim - mark it as swapped */
     pte_set_swap(&caller->mm->pgd[vicpgn], 0, swpfpn);
 
     /* Copy target frame from swap to the now-free victim frame in RAM */
     regs.a1 = SYSMEM_SWP_OP; // Operation: Swap memory
     regs.a2 = tgtfpn;        // Source frame (target frame in swap)
     regs.a3 = vicfpn;        // Destination frame (victim frame now free in RAM)
 
     /* SYSCALL 17 sys_memmap to bring target page from swap to RAM */
     syscall(caller, 17, &regs);
 
     /* Update page table entry for target page - mark it as present */
     pte_set_fpn(&mm->pgd[pgn], vicfpn);
 
     /* Add to FIFO queue for page replacement */
     enlist_pgn_node(&caller->mm->fifo_pgn, pgn);
   }
 
   *fpn = PAGING_FPN(mm->pgd[pgn]);
 
   return 0;
 }
 
 /*pg_getval - read value at given offset
  *@mm: memory region
  *@addr: virtual address to acess
  *@value: value
  *
  */
 int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller, int vmaid)
 {
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  pgn = vmaid ? (pgn + 1) : pgn;
  off = vmaid ? (256 - off) : off;
  int fpn;
 
   /* Get the page to MEMRAM, swap from MEMSWAP if needed */
   if (pg_getpage(mm, pgn, &fpn, caller) != 0)
     return -1; /* invalid page access */
 
   /* Calculate physical address */
   int phyaddr = (fpn << (PAGING_ADDR_OFFST_HIBIT + 1)) + off;
 
   /* Set up system call arguments for memory read */
   struct sc_regs regs;
   regs.a1 = SYSMEM_IO_READ; // Operation: Read from memory
   regs.a2 = phyaddr;        // Physical address to read from
   // regs.a3 = 0;             
   
   syscall(caller, 17, &regs);
 
   *data = (BYTE)regs.a3; 
 
   return 0;
 }
 
 /*pg_setval - write value to given offset
  *@mm: memory region
  *@addr: virtual address to acess
  *@value: value
  *
  */
 int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
 {
   int pgn = PAGING_PGN(addr);
   int off = PAGING_OFFST(addr);
   int fpn;
 
   /* Get the page to MEMRAM, swap from MEMSWAP if needed */
   if (pg_getpage(mm, pgn, &fpn, caller) != 0)
     return -1; /* invalid page access */
 
   /* Calculate physical address */
   int phyaddr = (fpn * PAGING_ADDR_OFFST_HIBIT + 1) + off;
 
   /* Set up system call arguments for memory write */
   struct sc_regs regs;
   regs.a1 = SYSMEM_IO_WRITE; // Operation: Write to memory
   regs.a2 = phyaddr;         // Physical address to write to
   regs.a3 = value;           // Value to write
 
   /* SYSCALL 17 sys_memmap to write data */
   syscall(caller, 17, &regs);
 
   return 0;
 }
 
 /*__read - read value in region memory
  *@caller: caller
  *@vmaid: ID vm area to alloc memory region
  *@offset: offset to acess in memory region
  *@rgid: memory region ID (used to identify variable in symbole table)
  *@size: allocated size
  *
  */
 int __read(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE *data)
 {
   struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
   if (currg == NULL)
   {
     return -1;
   }
   int vm_id = currg->vmaid;
   struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vm_id);
   if (cur_vma == NULL) /* Invalid memory identify */
     return -1;
   if (offset > abs(currg->rg_start - currg->rg_end))
   {
     printf("\tACCESS OUT OF REGION\n");
     return -1;
   }
   if (currg->vmaid == 0)
   {
     pg_getval(caller->mm, currg->rg_start + offset, data, caller, cur_vma->vm_id);
   }
   else
   {
     pg_getval(caller->mm, currg->rg_start - offset, data, caller, cur_vma->vm_id);
   }
   // pg_getval(caller->mm, currg->rg_start + offset, data, caller);
 
   return 0;
 }
 
 /*libread - PAGING-based read a region memory */
 int libread(
     struct pcb_t *proc, // Process executing the instruction
     uint32_t source,    // Index of source register
     uint32_t offset,    // Source address = [source] + [offset]
     uint32_t *destination)
 {
   BYTE data;
   int val = __read(proc, 0, source, offset, &data);
 
   /* Update result of reading action */
   *destination = (uint32_t)data; // Copy read byte to destination
 
 #ifdef IODUMP
   printf("read region=%d offset=%d value=%d\n", source, offset, data);
 #ifdef PAGETBL_DUMP
   print_pgtbl(proc, 0, -1); // print max TBL
 #endif
   MEMPHY_dump(proc->mram);
 #endif
 
   return val;
 }
 
 /*__write - write a region memory
  *@caller: caller
  *@vmaid: ID vm area to alloc memory region
  *@offset: offset to acess in memory region
  *@rgid: memory region ID (used to identify variable in symbole table)
  *@size: allocated size
  *
  */
 int __write(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE value)
 {
   struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
   struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
 
   if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
     return -1;
 
   pg_setval(caller->mm, currg->rg_start + offset, value, caller);
 
   return 0;
 }
 
 /*libwrite - PAGING-based write a region memory */
 int libwrite(
     struct pcb_t *proc,   // Process executing the instruction
     BYTE data,            // Data to be wrttien into memory
     uint32_t destination, // Index of destination register
     uint32_t offset)
 {
 #ifdef IODUMP
   printf("write region=%d offset=%d value=%d\n", destination, offset, data);
 #ifdef PAGETBL_DUMP
   print_pgtbl(proc, 0, -1); // print max TBL
 #endif
   MEMPHY_dump(proc->mram);
 #endif
 
   return __write(proc, 0, destination, offset, data);
 }
 
 /*free_pcb_memphy - collect all memphy of pcb
  *@caller: caller
  *@vmaid: ID vm area to alloc memory region
  *@incpgnum: number of page
  */
 int free_pcb_memph(struct pcb_t *caller)
 {
   int pagenum, fpn;
   uint32_t pte;
 
   for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
   {
     pte = caller->mm->pgd[pagenum];
 
     if (PAGING_PAGE_PRESENT(pte)) // Nếu trang hiện diện (trong RAM)
     {
       fpn = PAGING_PTE_FPN(pte);
       MEMPHY_put_freefp(caller->mram, fpn);
     }
     else
     { // Nếu trang không hiện diện (trong swap)
       fpn = PAGING_PTE_SWP(pte);
       MEMPHY_put_freefp(caller->active_mswp, fpn);
     }
   }
 
   return 0;
 }
 
 /*find_victim_page - find victim page
  *@caller: caller
  *@pgn: return page number
  *
  */
 int find_victim_page(struct mm_struct *mm, int *retpgn)
 {
   struct pgn_t *pg = mm->fifo_pgn;
 
   /* TODO: Implement the theorical mechanism to find the victim page */
   if (pg == NULL)
     return -1;
   struct pgn_t *prev = NULL;
   while (pg->pg_next)
   {
     prev = pg;
     pg = pg->pg_next;
   }
   *retpgn = pg->pgn;
   if (prev)
     prev->pg_next = NULL;
   else
   {
     mm->fifo_pgn = NULL;
   }
   free(pg);
 
   return 0;
 }
 
 /*get_free_vmrg_area - get a free vm region
  *@caller: caller
  *@vmaid: ID vm area to alloc memory region
  *@size: allocated size
  *
  */
 int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
 {
   size -= 1;
   struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
 
   if (cur_vma == NULL || cur_vma->vm_freerg_list == NULL)
     return -1;
 
   struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;
   /* Probe uninitialized newrg */
   newrg->rg_start = newrg->rg_end = -1;
 
   /* Traverse the list of free regions to find a fit space */
   while (rgit != NULL)
   {
     if (vmaid == 0) // Data region: allocate from low to high address
     {
       if (rgit->rg_start + size <= rgit->rg_end)
       {
         // Current region has enough space
         newrg->rg_start = rgit->rg_start;
         newrg->rg_end = rgit->rg_start + size;
 
         // Update left space in the chosen region
         if (rgit->rg_start + size < rgit->rg_end)
         {
           rgit->rg_start = rgit->rg_start + size;
         }
         else
         {
           // Use up all space, remove current node
           struct vm_rg_struct *nextrg = rgit->rg_next;
 
           if (nextrg != NULL)
           {
             rgit->rg_start = nextrg->rg_start;
             rgit->rg_end = nextrg->rg_end;
             rgit->rg_next = nextrg->rg_next;
 
             free(nextrg);
           }
           else
           {
             // End of free list
             rgit->rg_start = rgit->rg_end = 0;
             rgit->rg_next = NULL;
           }
         }
         break; // Region found, exit loop
       }
     }
     else if (vmaid == 1) // Heap region: allocate from high to low address
     {
       if (rgit->rg_end + size <= rgit->rg_start)
       {
         // Current region has enough space
         newrg->rg_start = rgit->rg_start;
         newrg->rg_end = rgit->rg_start - size;
 
         // Update remaining space in the chosen region
         if (rgit->rg_start - size > rgit->rg_end)
         {
           rgit->rg_start = rgit->rg_start - size;
         }
         else
         {
           // Use up all space, remove current node
           struct vm_rg_struct *nextrg = rgit->rg_next;
 
           if (nextrg != NULL)
           {
             rgit->rg_start = nextrg->rg_start;
             rgit->rg_end = nextrg->rg_end;
             rgit->rg_next = nextrg->rg_next;
 
             free(nextrg);
           }
           else
           {
             // End of free list
             rgit->rg_start = rgit->rg_end = caller->vmemsz;
             rgit->rg_next = NULL;
           }
         }
         break; // Region found, exit loop
       }
     }
 
     rgit = rgit->rg_next;
   }
 
   if (newrg->rg_start == -1)
     return -1;
 
   return 0;
 }
 
 // #endif