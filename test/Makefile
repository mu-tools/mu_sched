# mu_sched/test/Makefile

# -------------------------------------------------------------------
# Directories
# -------------------------------------------------------------------
BUILD_DIR := build
OBJ_DIR   := $(BUILD_DIR)/obj
BIN_DIR   := $(BUILD_DIR)/bin

# -------------------------------------------------------------------
# Toolchain and flags
# -------------------------------------------------------------------
CC      := gcc
CFLAGS  := -Wall -Wextra -Werror -O0 -g --coverage \
					 -I.. \
					 -I../inc \
					 -I../../mu_store/inc \
					 -I../../mu_thunk/inc \
					 -I../../mu_time/inc
LDFLAGS := --coverage

# -------------------------------------------------------------------
# Sources
# -------------------------------------------------------------------
SCHED_SRC   := ../src/mu_sched.c
POOL_SRC    := ../../mu_store/src/mu_pool.c
PQUEUE_SRC  := ../../mu_store/src/mu_pqueue.c
PVEC_SRC    := ../../mu_store/src/mu_pvec.c
SPSC_SRC    := ../../mu_store/src/mu_spsc.c
STORE_SRC   := ../../mu_store/src/mu_store.c
THUNK_SRC   := ../../mu_thunk/src/mu_thunk.c
TIME_SRC    := ../../mu_time/src/platform/mu_time_posix.c
TEST_SRC    := unity.c test_mu_sched.c

# -------------------------------------------------------------------
# Object files
# -------------------------------------------------------------------
OBJ := \
	$(OBJ_DIR)/mu_pool.o      \
	$(OBJ_DIR)/mu_pqueue.o    \
	$(OBJ_DIR)/mu_pvec.o      \
	$(OBJ_DIR)/mu_spsc.o      \
	$(OBJ_DIR)/mu_store.o     \
	$(OBJ_DIR)/mu_thunk.o     \
	$(OBJ_DIR)/mu_time_posix.o\
	$(OBJ_DIR)/mu_sched.o     \
	$(OBJ_DIR)/unity.o        \
	$(OBJ_DIR)/test_mu_sched.o

TEST_EXE := $(BIN_DIR)/test_mu_sched

# -------------------------------------------------------------------
# Phony targets
# -------------------------------------------------------------------
.PHONY: all test coverage clean

all: test

# -------------------------------------------------------------------
# Build & Run
# -------------------------------------------------------------------
tests: $(TEST_EXE)
	@echo ">>> Running mu_sched tests..."
	@./$(TEST_EXE)

$(TEST_EXE): $(OBJ) | $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ -o $@

# compile rules
$(OBJ_DIR)/mu_pool.o: $(POOL_SRC)    | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/mu_pqueue.o: $(PQUEUE_SRC)  | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/mu_pvec.o: $(PVEC_SRC)    | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/mu_spsc.o: $(SPSC_SRC)    | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/mu_store.o: $(STORE_SRC)    | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/mu_thunk.o: $(THUNK_SRC)  | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/mu_time_posix.o: $(TIME_SRC) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/mu_sched.o: $(SCHED_SRC)  | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/unity.o: unity.c          | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/test_mu_sched.o: test_mu_sched.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# make sure directories exist
$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

# -------------------------------------------------------------------
# Coverage report
# -------------------------------------------------------------------
coverage: test
	@echo ">>> Capturing coverage data..."
	lcov --capture \
			 --directory $(BUILD_DIR) \
			 --output-file coverage.info
	@echo ">>> Generating HTML report..."
	genhtml coverage.info --output-directory coverage_html
	@echo "open coverage_html/index.html"

# -------------------------------------------------------------------
# Clean
# -------------------------------------------------------------------
clean:
	@echo ">>> Cleaning build artifacts and coverage data"
	@rm -rf $(BUILD_DIR) coverage.info coverage_html