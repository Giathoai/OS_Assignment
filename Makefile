
INC = -Iinclude
LIB = -lpthread

SRC = src
OBJ = obj
INCLUDE = include

CC = gcc
DEBUG = -g
CFLAGS = -Wall -c $(DEBUG)
LFLAGS = -Wall $(DEBUG)

vpath %.c $(SRC)
vpath %.h $(INCLUDE)

MAKE = $(CC) $(INC) 

# Object files needed by modules
MEM_OBJ = $(addprefix $(OBJ)/, paging.o mem.o cpu.o loader.o)
SYSCALL_OBJ = $(addprefix $(OBJ)/, syscall.o sys_killall.o sys_mem.o sys_listsyscall.o)
OS_OBJ = $(addprefix $(OBJ)/, cpu.o mem.o loader.o queue.o os.o sched.o timer.o mm-vm.o mm.o mm-memphy.o libstd.o libmem.o)
OS_OBJ += $(SYSCALL_OBJ)
SCHED_OBJ = $(addprefix $(OBJ)/, cpu.o loader.o)
HEADER = $(wildcard $(INCLUDE)/*.h)

# Add directories for tests
TEST_DIR = test
TEST_OBJ_DIR = $(OBJ)/$(TEST_DIR)

# Define the memory test executable name
TEST_MEM_EXE = test_memory

# Objects for memory testing
TEST_MEM_OBJ = $(TEST_OBJ_DIR)/testvmem.o
MEM_TEST_DEPS = $(addprefix $(OBJ)/, mem.o mm-vm.o mm.o mm-memphy.o libstd.o libmem.o)

# Define the queue test executable name
TEST_QUEUE_EXE = test_queue

# Objects for queue testing
TEST_QUEUE_OBJ = $(TEST_OBJ_DIR)/testqueue.o
QUEUE_TEST_DEPS = $(addprefix $(OBJ)/, queue.o)

# Define the scheduler test executable name
TEST_SCHED_EXE = test_sched

# Objects for scheduler testing
TEST_SCHED_OBJ = $(TEST_OBJ_DIR)/testsched.o
SCHED_TEST_DEPS = $(addprefix $(OBJ)/, queue.o sched.o)
 
all: os
#mem sched os

# Just compile memory management modules
mem: $(MEM_OBJ)
	$(MAKE) $(LFLAGS) $(MEM_OBJ) -o mem $(LIB)

# Just compile scheduler
sched: $(SCHED_OBJ)
	$(MAKE) $(LFLAGS) $(MEM_OBJ) -o sched $(LIB)

# Compile syscall
syscalltbl.lst: $(SRC)/syscall.tbl
	@echo $(OS_OBJ)
	chmod +x $(SRC)/syscalltbl.sh
	$(SRC)/syscalltbl.sh $< $(SRC)/$@ 
#	mv $(OBJ)/syscalltbl.lst $(INCLUDE)/

# Compile the whole OS simulation
os: $(OBJ) syscalltbl.lst $(OS_OBJ)
	$(MAKE) $(LFLAGS) $(OS_OBJ) -o os $(LIB)

$(OBJ)/%.o: %.c ${HEADER} $(OBJ)
	$(MAKE) $(CFLAGS) $< -o $@

# Prepare objectives container
$(OBJ):
	mkdir -p $(OBJ)

clean:
	rm -f $(SRC)/*.lst
	rm -f $(OBJ)/*.o os sched mem
	rm -rf $(OBJ)

# Add test target
test: test_queue test_sched test_mem
	./$(TEST_QUEUE_EXE)
	./$(TEST_SCHED_EXE)
	./$(TEST_MEM_EXE)

# Target to run memory tests
test_mem: $(TEST_OBJ_DIR) $(TEST_MEM_OBJ) $(MEM_TEST_DEPS)
	$(MAKE) $(LFLAGS) $(TEST_MEM_OBJ) $(MEM_TEST_DEPS) -o $(TEST_MEM_EXE) $(LIB)

# Target to run queue tests
test_queue: $(TEST_OBJ_DIR) $(TEST_QUEUE_OBJ) $(QUEUE_TEST_DEPS)
	$(MAKE) $(LFLAGS) $(TEST_QUEUE_OBJ) $(QUEUE_TEST_DEPS) -o $(TEST_QUEUE_EXE) $(LIB)

# Target to run scheduler tests
test_sched: $(TEST_OBJ_DIR) $(TEST_SCHED_OBJ) $(SCHED_TEST_DEPS)
	$(MAKE) $(LFLAGS) $(TEST_SCHED_OBJ) $(SCHED_TEST_DEPS) -o $(TEST_SCHED_EXE) $(LIB)

# Create test object directory
$(TEST_OBJ_DIR):
	mkdir -p $(TEST_OBJ_DIR)

# Compile test files
$(TEST_OBJ_DIR)/%.o: $(TEST_DIR)/%.c $(HEADER)
	$(MAKE) $(CFLAGS) $< -o $@

# Add to clean target
clean: clean_test

clean_test:
	rm -f $(TEST_MEM_EXE)
	rm -rf $(TEST_OBJ_DIR)