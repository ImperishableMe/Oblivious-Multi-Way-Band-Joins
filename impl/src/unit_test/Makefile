# Unit Test Makefile - Depends on main SGX build
# This Makefile builds test applications that use the already-built enclave

# Paths
ENCLAVE_PATH = ..
SGX_SDK ?= /opt/intel/sgxsdk
SGX_MODE ?= HW
SGX_DEBUG ?= 1

# Enclave and generated files from main build
ENCLAVE_SIGNED = $(ENCLAVE_PATH)/enclave.signed.so
ENCLAVE_U_C = $(ENCLAVE_PATH)/app/Enclave_u.c
ENCLAVE_U_O = $(ENCLAVE_PATH)/app/Enclave_u.o

# Check if running in SGX simulation mode
ifeq ($(SGX_MODE), HW)
    SGX_LIB_PATH := $(SGX_SDK)/lib64
    URTS_LIB := sgx_urts
else
    SGX_LIB_PATH := $(SGX_SDK)/lib64
    URTS_LIB := sgx_urts_sim
endif

# Compiler settings
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -DDEBUG_LEVEL=5
INCLUDES = -I$(ENCLAVE_PATH) \
           -I$(ENCLAVE_PATH)/app \
           -I$(ENCLAVE_PATH)/enclave \
           -I$(ENCLAVE_PATH)/common \
           -I$(SGX_SDK)/include

# Linker settings
LDFLAGS = -L$(SGX_LIB_PATH) -l$(URTS_LIB) -lpthread -lstdc++fs

# Infrastructure source files from main project
INFRA_SRCS = $(ENCLAVE_PATH)/app/data_structures/join_constraint.cpp \
             $(ENCLAVE_PATH)/app/io/table_io.cpp \
             $(ENCLAVE_PATH)/app/io/converters.cpp \
             $(ENCLAVE_PATH)/app/crypto/crypto_utils.cpp \
             $(ENCLAVE_PATH)/app/data_structures/entry.cpp \
             $(ENCLAVE_PATH)/app/data_structures/table.cpp \
             $(ENCLAVE_PATH)/app/data_structures/join_condition.cpp \
             $(ENCLAVE_PATH)/app/debug_util.cpp

# Test source files
TEST_SRCS = test_main.cpp \
            test_encryption.cpp \
            test_window.cpp \
            test_comparators.cpp

# Table IO test
TABLE_IO_TEST_SRCS = test_table_io.cpp

# AES crypto test
AES_TEST_SRCS = test_aes_crypto.cpp

# Join correctness test
JOIN_TEST_SRCS = test_join_correctness.cpp \
                 test_utils/simple_join_executor.cpp \
                 test_utils/sqlite_ground_truth.cpp \
                 test_utils/join_result_comparator.cpp \
                 $(ENCLAVE_PATH)/app/utils/query_parser.cpp \
                 $(ENCLAVE_PATH)/app/utils/query_tokenizer.cpp \
                 $(ENCLAVE_PATH)/app/utils/inequality_parser.cpp \
                 $(ENCLAVE_PATH)/app/utils/condition_merger.cpp \
                 $(ENCLAVE_PATH)/app/utils/join_tree_builder.cpp

# Bottom-up phase test
BOTTOM_UP_TEST_SRCS = ../test/unit/test_bottom_up.cpp \
                      ../test/utils/subtree_verifier.cpp \
                      ../test/utils/simple_join_executor.cpp \
                      $(ENCLAVE_PATH)/app/algorithms/bottom_up_phase.cpp \
                      $(ENCLAVE_PATH)/app/query/query_parser.cpp \
                      $(ENCLAVE_PATH)/app/query/query_tokenizer.cpp \
                      $(ENCLAVE_PATH)/app/query/inequality_parser.cpp \
                      $(ENCLAVE_PATH)/app/query/condition_merger.cpp \
                      $(ENCLAVE_PATH)/app/data_structures/join_tree_builder.cpp \
                      $(ENCLAVE_PATH)/app/data_structures/join_attribute_setter.cpp

# Top-down phase test
TOP_DOWN_TEST_SRCS = ../test/unit/test_top_down.cpp \
                     $(ENCLAVE_PATH)/app/algorithms/bottom_up_phase.cpp \
                     $(ENCLAVE_PATH)/app/algorithms/top_down_phase.cpp \
                     $(ENCLAVE_PATH)/app/query/query_parser.cpp \
                     $(ENCLAVE_PATH)/app/query/query_tokenizer.cpp \
                     $(ENCLAVE_PATH)/app/query/inequality_parser.cpp \
                     $(ENCLAVE_PATH)/app/query/condition_merger.cpp \
                     $(ENCLAVE_PATH)/app/data_structures/join_tree_builder.cpp \
                     $(ENCLAVE_PATH)/app/data_structures/join_attribute_setter.cpp

# Full join test (includes top-down phase)
FULL_JOIN_TEST_SRCS = ../test/unit/test_full_join.cpp \
                      ../test/utils/simple_join_executor.cpp \
                      $(ENCLAVE_PATH)/app/algorithms/bottom_up_phase.cpp \
                      $(ENCLAVE_PATH)/app/algorithms/top_down_phase.cpp \
                      $(ENCLAVE_PATH)/app/query/query_parser.cpp \
                      $(ENCLAVE_PATH)/app/query/query_tokenizer.cpp \
                      $(ENCLAVE_PATH)/app/query/inequality_parser.cpp \
                      $(ENCLAVE_PATH)/app/query/condition_merger.cpp \
                      $(ENCLAVE_PATH)/app/data_structures/join_tree_builder.cpp \
                      $(ENCLAVE_PATH)/app/data_structures/join_attribute_setter.cpp

DISTRIBUTE_TEST_SRCS = ../test/unit/test_distribute_expand.cpp \
                       $(ENCLAVE_PATH)/app/algorithms/distribute_expand.cpp

# Comprehensive encryption test
ENCRYPTION_COMP_TEST_SRCS = test_encryption_comprehensive.cpp

# Simple encryption test
ENCRYPTION_SIMPLE_TEST_SRCS = test_encryption_simple.cpp

# Object files needed from main build
MAIN_OBJS = $(ENCLAVE_U_O)

# Test executables
TEST_TARGET = sgx_test
TABLE_IO_TEST_TARGET = test_table_io
AES_TEST_TARGET = test_aes_crypto
JOIN_TEST_TARGET = test_join_correctness
BOTTOM_UP_TEST_TARGET = test_bottom_up
TOP_DOWN_TEST_TARGET = test_top_down
FULL_JOIN_TEST_TARGET = test_full_join
DISTRIBUTE_TEST_TARGET = test_distribute_expand
ENCRYPTION_COMP_TEST_TARGET = test_encryption_comprehensive
ENCRYPTION_SIMPLE_TEST_TARGET = test_encryption_simple

# Default target
all: check-enclave $(TEST_TARGET) $(TABLE_IO_TEST_TARGET) $(AES_TEST_TARGET) $(JOIN_TEST_TARGET) $(BOTTOM_UP_TEST_TARGET) $(TOP_DOWN_TEST_TARGET) $(FULL_JOIN_TEST_TARGET) $(DISTRIBUTE_TEST_TARGET) $(ENCRYPTION_COMP_TEST_TARGET)

# Check if enclave is built and create symlink for testing
check-enclave:
	@if [ ! -f $(ENCLAVE_SIGNED) ]; then \
		echo "Error: Enclave not built. Building main project first..."; \
		$(MAKE) -C $(ENCLAVE_PATH); \
	fi
	@if [ ! -f $(ENCLAVE_U_O) ]; then \
		echo "Error: Enclave_u.o not found. Building main project first..."; \
		$(MAKE) -C $(ENCLAVE_PATH); \
	fi
	@echo "Creating symlink to enclave for testing..."
	@ln -sf $(ENCLAVE_SIGNED) enclave.signed.so

# Build test executable
$(TEST_TARGET): check-enclave $(TEST_SRCS)
	@echo "Building unit tests..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(TEST_SRCS) $(MAIN_OBJS) $(LDFLAGS) -o $@
	@echo "Build complete. Run with: ./$(TEST_TARGET)"

# Build table IO test executable
$(TABLE_IO_TEST_TARGET): check-enclave $(TABLE_IO_TEST_SRCS) $(INFRA_SRCS)
	@echo "Building table IO tests..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(TABLE_IO_TEST_SRCS) $(INFRA_SRCS) $(MAIN_OBJS) $(LDFLAGS) -o $@
	@echo "Build complete. Run with: ./$(TABLE_IO_TEST_TARGET)"

# Build AES crypto test executable
$(AES_TEST_TARGET): check-enclave $(AES_TEST_SRCS)
	@echo "Building AES crypto tests..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(AES_TEST_SRCS) $(MAIN_OBJS) $(LDFLAGS) -o $@
	@echo "Build complete. Run with: ./$(AES_TEST_TARGET)"

# Build join correctness test executable
$(JOIN_TEST_TARGET): check-enclave $(JOIN_TEST_SRCS) $(INFRA_SRCS)
	@echo "Building join correctness tests..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(JOIN_TEST_SRCS) $(INFRA_SRCS) $(MAIN_OBJS) $(LDFLAGS) -lsqlite3 -o $@
	@echo "Build complete. Run with: ./$(JOIN_TEST_TARGET)"

# Build bottom-up phase test executable
$(BOTTOM_UP_TEST_TARGET): check-enclave $(BOTTOM_UP_TEST_SRCS) $(INFRA_SRCS)
	@echo "Building bottom-up phase tests..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(BOTTOM_UP_TEST_SRCS) $(INFRA_SRCS) $(MAIN_OBJS) $(LDFLAGS) -o $@
	@echo "Build complete. Run with: ./$(BOTTOM_UP_TEST_TARGET)"

# Build top-down phase test executable
$(TOP_DOWN_TEST_TARGET): check-enclave $(TOP_DOWN_TEST_SRCS) $(INFRA_SRCS)
	@echo "Building top-down phase tests..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(TOP_DOWN_TEST_SRCS) $(INFRA_SRCS) $(MAIN_OBJS) $(LDFLAGS) -o $@
	@echo "Build complete. Run with: ./$(TOP_DOWN_TEST_TARGET)"

# Build full join test executable
$(FULL_JOIN_TEST_TARGET): check-enclave $(FULL_JOIN_TEST_SRCS) $(INFRA_SRCS)
	@echo "Building full join tests..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(FULL_JOIN_TEST_SRCS) $(INFRA_SRCS) $(MAIN_OBJS) $(LDFLAGS) -o $@
	@echo "Build complete. Run with: ./$(FULL_JOIN_TEST_TARGET)"

# Build distribute-expand test executable
$(DISTRIBUTE_TEST_TARGET): check-enclave $(DISTRIBUTE_TEST_SRCS) $(INFRA_SRCS)
	@echo "Building distribute-expand tests..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DISTRIBUTE_TEST_SRCS) $(INFRA_SRCS) $(MAIN_OBJS) $(LDFLAGS) -o $@
	@echo "Build complete. Run with: ./$(DISTRIBUTE_TEST_TARGET)"

# Build comprehensive encryption test executable
$(ENCRYPTION_COMP_TEST_TARGET): check-enclave $(ENCRYPTION_COMP_TEST_SRCS) $(INFRA_SRCS)
	@echo "Building comprehensive encryption tests..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(ENCRYPTION_COMP_TEST_SRCS) $(INFRA_SRCS) $(MAIN_OBJS) $(LDFLAGS) -o $@
	@echo "Build complete. Run with: ./$(ENCRYPTION_COMP_TEST_TARGET)"

# Build simple encryption test executable
$(ENCRYPTION_SIMPLE_TEST_TARGET): check-enclave $(ENCRYPTION_SIMPLE_TEST_SRCS) $(INFRA_SRCS)
	@echo "Building simple encryption test..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(ENCRYPTION_SIMPLE_TEST_SRCS) $(INFRA_SRCS) $(MAIN_OBJS) $(LDFLAGS) -o $@
	@echo "Build complete. Run with: ./$(ENCRYPTION_SIMPLE_TEST_TARGET)"

# Run tests
run: $(TEST_TARGET)
	@echo "==================================="
	@echo "Running SGX Unit Tests"
	@echo "==================================="
	./$(TEST_TARGET)

# Run specific test suites
run-encryption: $(TEST_TARGET)
	./$(TEST_TARGET) --suite encryption

run-window: $(TEST_TARGET)
	./$(TEST_TARGET) --suite window

run-comparators: $(TEST_TARGET)
	./$(TEST_TARGET) --suite comparators

run-table-io: $(TABLE_IO_TEST_TARGET)
	@echo "==================================="
	@echo "Running Table I/O Tests"
	@echo "==================================="
	./$(TABLE_IO_TEST_TARGET)

run-aes: $(AES_TEST_TARGET)
	@echo "==================================="
	@echo "Running AES Crypto Tests"
	@echo "==================================="
	./$(AES_TEST_TARGET)

run-join: $(JOIN_TEST_TARGET)
	@echo "==================================="
	@echo "Running Join Correctness Tests"
	@echo "==================================="
	./$(JOIN_TEST_TARGET)

run-bottom-up: $(BOTTOM_UP_TEST_TARGET)
	@echo "==================================="
	@echo "Running Bottom-Up Phase Tests"
	@echo "==================================="
	./$(BOTTOM_UP_TEST_TARGET)

# Clean test files only (not the main build)
clean:
	@echo "Cleaning unit test files..."
	rm -f $(TEST_TARGET) $(TABLE_IO_TEST_TARGET) $(AES_TEST_TARGET) $(JOIN_TEST_TARGET) $(BOTTOM_UP_TEST_TARGET) $(TOP_DOWN_TEST_TARGET) $(FULL_JOIN_TEST_TARGET) $(DISTRIBUTE_TEST_TARGET) $(ENCRYPTION_COMP_TEST_TARGET) *.o
	rm -f enclave.signed.so  # Remove symlink
	@echo "Note: Main enclave files are preserved"

# Clean everything including main build
clean-all:
	$(MAKE) -C $(ENCLAVE_PATH) clean
	rm -f $(TEST_TARGET) *.o

# Help
help:
	@echo "Unit Test Makefile - Uses pre-built enclave from main project"
	@echo ""
	@echo "Targets:"
	@echo "  all              - Check enclave and build all tests"
	@echo "  run              - Build and run SGX tests"
	@echo "  run-encryption   - Run encryption tests only"
	@echo "  run-window       - Run window function tests only"
	@echo "  run-comparators  - Run comparator tests only"
	@echo "  run-table-io     - Run table I/O and infrastructure tests"
	@echo "  run-join         - Run join correctness tests with SQLite"
	@echo "  clean            - Clean test files only"
	@echo "  clean-all        - Clean everything including main build"
	@echo "  help             - Show this help"
	@echo ""
	@echo "The enclave must be built first via the main Makefile."
	@echo "This test suite uses: $(ENCLAVE_SIGNED)"

.PHONY: all check-enclave run run-encryption run-window run-comparators run-table-io clean clean-all help