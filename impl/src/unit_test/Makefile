# Makefile for unit tests (source-level)

# Compiler settings
CXX = g++
CC = gcc
CXXFLAGS = -std=c++11 -Wall -Wextra -I.. -I../enclave -I../common -I../app
CFLAGS = -Wall -Wextra -I.. -I../enclave -I../common

# Test executables
TEST_TARGETS = test_encryption

# Source files
ENCRYPTION_TEST_SRCS = test_encryption.cpp ../enclave/crypto/entry_crypto.c ../app/converters.cpp

# Default target
all: $(TEST_TARGETS)

# Build test_encryption
test_encryption: test_encryption.cpp ../enclave/crypto/entry_crypto.c ../app/converters.cpp
	$(CXX) $(CXXFLAGS) test_encryption.cpp ../enclave/crypto/entry_crypto.c ../app/converters.cpp -o test_encryption

# Run all tests
run: all
	@echo "=== Running SGX Encryption Tests ==="
	./test_encryption

# Run encryption tests (same as run now)
run_encryption: test_encryption
	./test_encryption

# Clean up
clean:
	rm -f $(TEST_TARGETS) *.o

# Help target
help:
	@echo "Available targets:"
	@echo "  all                          - Build all tests"
	@echo "  test_encryption              - Build SGX encryption tests (includes field coverage)"
	@echo "  run                          - Build and run all SGX tests"
	@echo "  run_encryption               - Build and run SGX encryption tests"
	@echo "  clean                        - Remove built files"
	@echo "  help                         - Show this help message"

.PHONY: all run run_encryption clean help