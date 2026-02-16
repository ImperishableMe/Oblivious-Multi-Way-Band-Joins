#
# Main Makefile for TDX-Compatible Oblivious Multi-Way Join
# Migrated from SGX to TDX architecture
#

######## Build Flags ########

# Debug mode (default: off for performance)
DEBUG ?= 0
SLIM_ENTRY ?= 0

# Common compiler flags
COMMON_FLAGS := -Wall -Wextra -Wno-attributes

# Debug/Release flags
ifeq ($(DEBUG), 1)
	COMMON_FLAGS += -O0 -g -DDEBUG
else
	COMMON_FLAGS += -O2 -DNDEBUG
endif

######## App Settings ########

# C++ source files
App_Cpp_Files := main/sgx_join/main.cpp \
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
                 app/query/filter_condition.cpp \
                 app/algorithms/bottom_up_phase.cpp \
                 app/algorithms/top_down_phase.cpp \
                 app/algorithms/distribute_expand.cpp \
                 app/algorithms/align_concat.cpp \
                 app/algorithms/oblivious_join.cpp \
                 app/algorithms/merge_sort_manager.cpp \
                 app/algorithms/shuffle_manager.cpp \
                 app/debug_stubs.cpp \
                 app/core_logic_callbacks.cpp

# C source files from core_logic (merged from enclave)
App_C_Files := app/core_logic/algorithms/min_heap.c \
               app/core_logic/algorithms/heap_sort.c \
               app/core_logic/algorithms/k_way_merge.c \
               app/core_logic/algorithms/k_way_shuffle.c \
               app/core_logic/algorithms/oblivious_waksman.c \
               app/core_logic/operations/comparators.c \
               app/core_logic/operations/merge_comparators.c \
               app/core_logic/operations/window_functions.c \
               app/core_logic/operations/transform_functions.c \
               app/core_logic/operations/distribute_functions.c

# Include paths
App_Include_Paths := -Icommon -Iapp -Iapp/core_logic

# Debug level setting
ifndef DEBUG_LEVEL
    DEBUG_LEVEL := 0
endif

# Compiler flags
App_Compile_CFlags := $(COMMON_FLAGS) -fPIC $(App_Include_Paths) -DDEBUG_LEVEL=$(DEBUG_LEVEL)
App_Compile_CXXFlags := $(App_Compile_CFlags) -std=c++11

# Add SLIM_ENTRY flag if specified
ifeq ($(SLIM_ENTRY), 1)
    App_Compile_CFlags += -DSLIM_ENTRY
    App_Compile_CXXFlags += -DSLIM_ENTRY
endif

# Link flags (just pthread, no SGX libraries)
App_Link_Flags := -lpthread

# All object files
App_Objects := $(App_Cpp_Files:.cpp=.o) $(App_C_Files:.c=.o)

App_Name := sgx_app

######## Build Targets ########

.PHONY: all build clean

all: $(App_Name)
	@echo "Build complete!"

build: all

######## App Build ########

# Compile C++ source files
main/%.o: main/%.cpp
	@$(CXX) $(App_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

app/%.o: app/%.cpp
	@$(CXX) $(App_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

# Compile C source files from core_logic
app/core_logic/%.o: app/core_logic/%.c
	@$(CC) $(App_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

# Link app
$(App_Name): $(App_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

######## Test Programs ########

# Test program settings
Test_Include_Paths := -I. -Icommon -Iapp -Iapp/core_logic -Iobligraph/include
Test_Compile_CFlags := $(COMMON_FLAGS) -fPIC $(Test_Include_Paths)
Test_Compile_CXXFlags := $(Test_Compile_CFlags) -std=c++20

# Test programs
Test_Join_Objects := tests/integration/test_join.o
Sqlite_Baseline_Objects := tests/baseline/sqlite_baseline.o
Test_Merge_Sort_Objects := tests/unit/test_merge_sort.o
Test_Waksman_Objects := tests/unit/test_waksman_shuffle.o
Test_Waksman_Dist_Objects := tests/unit/test_waksman_distribution.o
Test_Shuffle_Manager_Objects := tests/unit/test_shuffle_manager.o
Benchmark_Sorting_Objects := tests/performance/benchmark_sorting.o

# Common objects needed by test programs (reuse from main app)
Test_Common_Objects := app/file_io/converters.o \
                      app/file_io/table_io.o \
                      app/data_structures/entry.o \
                      app/data_structures/table.o \
                      app/join/join_condition.o \
                      app/join/join_attribute_setter.o \
                      app/algorithms/merge_sort_manager.o \
                      app/algorithms/shuffle_manager.o \
                      app/core_logic_callbacks.o \
                      app/debug_stubs.o \
                      $(App_C_Files:.c=.o)

# Test executables
test_join: $(Test_Join_Objects) $(Test_Common_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

sqlite_baseline: $(Sqlite_Baseline_Objects) $(Test_Common_Objects) app/query/query_parser.o app/query/query_tokenizer.o app/query/inequality_parser.o app/query/condition_merger.o app/join/join_constraint.o app/join/join_condition.o
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

benchmark_sorting: $(Benchmark_Sorting_Objects) $(Test_Common_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
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

tests/performance/%.o: tests/performance/%.cpp
	@$(CXX) $(Test_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

# Build all tests
tests: test_join sqlite_baseline test_merge_sort test_waksman_shuffle test_waksman_distribution test_shuffle_manager
	@echo "All tests built successfully"

######## Clean ########

clean:
	@rm -f $(App_Name)
	@rm -f $(App_Objects)
	@rm -f test_join sqlite_baseline test_merge_sort test_waksman_shuffle test_waksman_distribution test_shuffle_manager benchmark_sorting
	@rm -f $(Test_Join_Objects) $(Sqlite_Baseline_Objects) $(Test_Merge_Sort_Objects) $(Test_Waksman_Objects) $(Test_Waksman_Dist_Objects) $(Test_Shuffle_Manager_Objects) $(Benchmark_Sorting_Objects)
	@rm -f app/core_logic/**/*.o