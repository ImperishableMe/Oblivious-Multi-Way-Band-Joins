#
# Main Makefile for Memory Constrained Oblivious Multi-Way Join
# Plain Linux build (no SGX - uses OpenSSL for crypto)
#

######## Build Settings ########

# Debug configuration
DEBUG ?= 0
DEBUG_LEVEL ?= 0

# Compiler flags
COMMON_FLAGS := -m64 -Wall -Wextra -Winit-self -Wpointer-arith -Wreturn-type \
                -Waddress -Wsequence-point -Wformat-security

ifeq ($(DEBUG), 1)
	COMMON_FLAGS += -O0 -g -DDEBUG -UNDEBUG
else
	COMMON_FLAGS += -O2 -DNDEBUG -UDEBUG
endif

COMMON_CFLAGS := $(COMMON_FLAGS) -Wjump-misses-init -Wstrict-prototypes
COMMON_CXXFLAGS := $(COMMON_FLAGS) -Wnon-virtual-dtor -std=c++11

######## App Settings ########

# App C++ source files
App_Cpp_Files := main/sgx_join/main.cpp \
                 app/crypto/crypto_utils.cpp \
                 app/file_io/converters.cpp \
                 app/file_io/table_io.cpp \
                 app/data_structures/entry.cpp \
                 app/data_structures/table.cpp \
                 app/join/join_condition.cpp \
                 app/join/join_constraint.cpp \
                 app/join/join_tree_builder.cpp \
                 app/join/join_attribute_setter.cpp \
                 app/query/query_tokenizer.cpp \
                 app/query/query_parser.cpp \
                 app/query/inequality_parser.cpp \
                 app/query/condition_merger.cpp \
                 app/algorithms/bottom_up_phase.cpp \
                 app/algorithms/top_down_phase.cpp \
                 app/algorithms/distribute_expand.cpp \
                 app/algorithms/align_concat.cpp \
                 app/algorithms/oblivious_join.cpp \
                 app/algorithms/merge_sort_manager.cpp \
                 app/algorithms/shuffle_manager.cpp \
                 app/batch/ecall_batch_collector.cpp \
                 app/batch/ecall_wrapper.cpp \
                 app/debug_stubs.cpp \
                 app/sgx_compat/sgx_urts.cpp \
                 app/sgx_compat/sgx_ecalls.cpp

# App C source files (from enclave_logic)
App_C_Files := app/enclave_logic/crypto/aes_crypto.c \
               app/enclave_logic/crypto/crypto_helpers.c \
               app/enclave_logic/crypto/aes_operations.c \
               app/enclave_logic/algorithms/k_way_merge.c \
               app/enclave_logic/algorithms/k_way_shuffle.c \
               app/enclave_logic/algorithms/heap_sort.c \
               app/enclave_logic/algorithms/oblivious_waksman.c \
               app/enclave_logic/algorithms/min_heap.c \
               app/enclave_logic/operations/comparators.c \
               app/enclave_logic/operations/window_functions.c \
               app/enclave_logic/operations/transform_functions.c \
               app/enclave_logic/operations/distribute_functions.c \
               app/enclave_logic/operations/merge_comparators.c \
               app/enclave_logic/batch/batch_dispatcher.c \
               app/enclave_logic/debug_wrapper.c \
               app/enclave_logic/test/test_ecalls.c \
               app/enclave_logic/test/test_crypto_ecalls.c

# Include paths
App_Include_Paths := -Icommon -Iapp -Iapp/sgx_compat -Iapp/enclave_logic

# Compile flags
App_Compile_CFlags := -fPIC -Wno-attributes $(App_Include_Paths) -DENCLAVE_BUILD -include sgx_compat/sgx_types.h
App_Compile_CXXFlags := -fPIC -Wno-attributes $(App_Include_Paths) -DENCLAVE_BUILD -std=c++11 -DDEBUG_LEVEL=$(DEBUG_LEVEL)

# Add SLIM_ENTRY flag if specified
ifdef SLIM_ENTRY
    App_Compile_CFlags += -DSLIM_ENTRY
    App_Compile_CXXFlags += -DSLIM_ENTRY
endif

# Link flags - use OpenSSL instead of SGX libraries
App_Link_Flags := -lpthread -lssl -lcrypto

# Objects
App_Cpp_Objects := $(App_Cpp_Files:.cpp=.o)
App_C_Objects := $(App_C_Files:.c=.o)
App_Objects := $(App_Cpp_Objects) $(App_C_Objects)

App_Name := sgx_app
Encrypt_Tool := encrypt_tables

######## Build Targets ########

.PHONY: all build clean tests

all: $(App_Name) $(Encrypt_Tool)
	@echo "Build complete!"

build: all

######## App Build ########

# Compile app C++ source files
main/%.o: main/%.cpp
	@$(CXX) $(COMMON_CXXFLAGS) $(App_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

main/tools/%.o: main/tools/%.cpp
	@$(CXX) $(COMMON_CXXFLAGS) $(App_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

app/%.o: app/%.cpp
	@$(CXX) $(COMMON_CXXFLAGS) $(App_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

# Compile app C source files (from enclave_logic)
app/enclave_logic/%.o: app/enclave_logic/%.c
	@$(CC) $(COMMON_CFLAGS) $(App_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

app/enclave_logic/crypto/%.o: app/enclave_logic/crypto/%.c
	@$(CC) $(COMMON_CFLAGS) $(App_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

app/enclave_logic/algorithms/%.o: app/enclave_logic/algorithms/%.c
	@$(CC) $(COMMON_CFLAGS) $(App_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

app/enclave_logic/operations/%.o: app/enclave_logic/operations/%.c
	@$(CC) $(COMMON_CFLAGS) $(App_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

app/enclave_logic/batch/%.o: app/enclave_logic/batch/%.c
	@$(CC) $(COMMON_CFLAGS) $(App_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

app/enclave_logic/test/%.o: app/enclave_logic/test/%.c
	@$(CC) $(COMMON_CFLAGS) $(App_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

# Link app
$(App_Name): $(App_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

# Build encrypt_tables utility
Encrypt_Tool_Objects := main/tools/encrypt_tables.o \
                       app/file_io/table_io.o \
                       app/file_io/converters.o \
                       app/crypto/crypto_utils.o \
                       app/data_structures/entry.o \
                       app/data_structures/table.o \
                       app/join/join_condition.o \
                       app/join/join_constraint.o \
                       app/join/join_attribute_setter.o \
                       app/algorithms/merge_sort_manager.o \
                       app/algorithms/shuffle_manager.o \
                       app/batch/ecall_batch_collector.o \
                       app/batch/ecall_wrapper.o \
                       app/debug_stubs.o \
                       app/sgx_compat/sgx_urts.o \
                       app/sgx_compat/sgx_ecalls.o \
                       $(App_C_Objects)

$(Encrypt_Tool): $(Encrypt_Tool_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

######## Test Programs ########

# Test program settings
Test_Include_Paths := -I. -Icommon -Iapp -Iapp/sgx_compat
Test_Compile_CFlags := $(COMMON_CFLAGS) -fPIC -Wno-attributes $(Test_Include_Paths)
Test_Compile_CXXFlags := $(Test_Compile_CFlags) -std=c++17

# Test programs
Test_Join_Objects := tests/integration/test_join.o
Test_Join_Batch_Objects := tests/integration/test_join_batch.o
Sqlite_Baseline_Objects := tests/baseline/sqlite_baseline.o
Test_Merge_Sort_Objects := tests/unit/test_merge_sort.o
Test_Waksman_Objects := tests/unit/test_waksman_shuffle.o
Test_Waksman_Dist_Objects := tests/unit/test_waksman_distribution.o
Test_Shuffle_Manager_Objects := tests/unit/test_shuffle_manager.o

# Common objects needed by test programs (reuse from main app)
Test_Common_Objects := app/crypto/crypto_utils.o \
                      app/file_io/converters.o \
                      app/file_io/table_io.o \
                      app/data_structures/entry.o \
                      app/data_structures/table.o \
                      app/join/join_condition.o \
                      app/join/join_attribute_setter.o \
                      app/algorithms/merge_sort_manager.o \
                      app/algorithms/shuffle_manager.o \
                      app/batch/ecall_batch_collector.o \
                      app/batch/ecall_wrapper.o \
                      app/debug_stubs.o \
                      app/sgx_compat/sgx_urts.o \
                      app/sgx_compat/sgx_ecalls.o \
                      $(App_C_Objects)

# Test executables
test_join: $(Test_Join_Objects) $(Test_Common_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags) -lsqlite3
	@echo "LINK =>  $@"

sqlite_baseline: $(Sqlite_Baseline_Objects) $(Test_Common_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags) -lsqlite3
	@echo "LINK =>  $@"

test_merge_sort: $(Test_Merge_Sort_Objects) $(Test_Common_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

test_waksman_shuffle: $(Test_Waksman_Objects) $(Test_Common_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

test_waksman_distribution: $(Test_Waksman_Dist_Objects) $(Test_Common_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

test_shuffle_manager: $(Test_Shuffle_Manager_Objects) $(Test_Common_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

test_join_batch: $(Test_Join_Batch_Objects) $(Test_Common_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags) -lsqlite3
	@echo "LINK =>  $@"

# Compile test source files
tests/integration/%.o: tests/integration/%.cpp
	@$(CXX) $(Test_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

tests/baseline/%.o: tests/baseline/%.cpp
	@$(CXX) $(Test_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

tests/unit/%.o: tests/unit/%.cpp
	@$(CXX) $(Test_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

# Build all tests
tests: test_join sqlite_baseline test_merge_sort test_waksman_shuffle test_waksman_distribution test_shuffle_manager
	@echo "All tests built successfully"

######## Clean ########

clean:
	@rm -f $(App_Name) $(Encrypt_Tool)
	@rm -f $(App_Cpp_Objects) $(App_C_Objects) main/tools/*.o
	@rm -f test_join sqlite_baseline test_merge_sort test_waksman_shuffle test_waksman_distribution test_shuffle_manager test_join_batch
	@rm -f $(Test_Join_Objects) $(Test_Join_Batch_Objects) $(Sqlite_Baseline_Objects)
	@rm -f $(Test_Merge_Sort_Objects) $(Test_Waksman_Objects) $(Test_Waksman_Dist_Objects) $(Test_Shuffle_Manager_Objects)
	@rm -f .config_*
	@echo "Clean complete!"
