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
CXXFLAGS = -std=c++17 -Wall -Wextra
INCLUDES = -I$(ENCLAVE_PATH) \
           -I$(ENCLAVE_PATH)/app \
           -I$(ENCLAVE_PATH)/enclave \
           -I$(ENCLAVE_PATH)/common \
           -I$(SGX_SDK)/include

# Linker settings
LDFLAGS = -L$(SGX_LIB_PATH) -l$(URTS_LIB) -lpthread -lstdc++fs

# Infrastructure source files from main project
INFRA_SRCS = $(ENCLAVE_PATH)/app/utils/join_constraint.cpp \
             $(ENCLAVE_PATH)/app/utils/table_io.cpp \
             $(ENCLAVE_PATH)/app/converters.cpp \
             $(ENCLAVE_PATH)/app/crypto_utils.cpp \
             $(ENCLAVE_PATH)/app/types.cpp

# Test source files
TEST_SRCS = test_main.cpp \
            test_encryption.cpp \
            test_window.cpp \
            test_comparators.cpp

# Table IO test
TABLE_IO_TEST_SRCS = test_table_io.cpp

# AES crypto test
AES_TEST_SRCS = test_aes_crypto.cpp

# Object files needed from main build
MAIN_OBJS = $(ENCLAVE_U_O)

# Test executables
TEST_TARGET = sgx_test
TABLE_IO_TEST_TARGET = test_table_io
AES_TEST_TARGET = test_aes_crypto

# Default target
all: check-enclave $(TEST_TARGET) $(TABLE_IO_TEST_TARGET) $(AES_TEST_TARGET)

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

# Clean test files only (not the main build)
clean:
	@echo "Cleaning unit test files..."
	rm -f $(TEST_TARGET) $(TABLE_IO_TEST_TARGET) $(AES_TEST_TARGET) *.o
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
	@echo "  clean            - Clean test files only"
	@echo "  clean-all        - Clean everything including main build"
	@echo "  help             - Show this help"
	@echo ""
	@echo "The enclave must be built first via the main Makefile."
	@echo "This test suite uses: $(ENCLAVE_SIGNED)"

.PHONY: all check-enclave run run-encryption run-window run-comparators run-table-io clean clean-all help