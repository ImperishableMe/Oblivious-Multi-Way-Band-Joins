#
# Main Makefile for TDX-Compatible Oblivious Multi-Way Join
# Migrated from SGX to TDX architecture
#

######## Build Flags ########

# Debug mode (default: off for performance)
DEBUG ?= 0
SLIM_ENTRY ?= 0

# Out-of-tree build support: when BUILD_DIR is set (e.g. the slim build),
# object files are written under $(BUILD_DIR)/ instead of alongside the sources,
# so a build with different -D flags cannot contaminate the default build's
# objects (and vice-versa). Empty BUILD_DIR reproduces the original behavior
# exactly: objects live next to their sources.
BUILD_DIR ?=
ifeq ($(BUILD_DIR),)
    OBJ_PREFIX :=
else
    OBJ_PREFIX := $(BUILD_DIR)/
endif

# Optional compile-time override of MAX_ATTRIBUTES (the fixed per-entry
# attribute-array capacity in common/constants.h). Empty => use the default (64).
# The slim build sets this to shrink the per-entry footprint.
MAX_ATTRIBUTES ?=

# Common compiler flags
COMMON_FLAGS := -Wall -Wextra -Wno-attributes -mavx2 -DUSE_AVX2

# Debug/Release flags
ifeq ($(DEBUG), 1)
	COMMON_FLAGS += -O0 -g -DDEBUG
else
	COMMON_FLAGS += -O3 -DNDEBUG
endif

# Apply the MAX_ATTRIBUTES override (if any) to every translation unit.
ifneq ($(MAX_ATTRIBUTES),)
	COMMON_FLAGS += -DMAX_ATTRIBUTES=$(MAX_ATTRIBUTES)
endif

######## App Settings ########

# C++ source files
App_Cpp_Files := main/sgx_join/main.cpp \
                 app/file_io/converters.cpp \
                 app/file_io/table_io.cpp \
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
                 app/debug_stubs.cpp \
                 obligraph/src/par_obl_primitives.cpp

# C source files from core_logic (merged from enclave)
App_C_Files := app/core_logic/algorithms/min_heap.c \
               app/core_logic/algorithms/heap_sort.c \
               app/core_logic/algorithms/oblivious_waksman.c \
               app/core_logic/operations/comparators.c \
               app/core_logic/operations/merge_comparators.c \
               app/core_logic/operations/window_functions.c \
               app/core_logic/operations/transform_functions.c \
               app/core_logic/operations/distribute_functions.c

# Include paths
# obligraph/include is added with -isystem so its pre-existing warnings
# (sign-compare, unused-variable in ObliviousArrayAccessSimd, etc.) don't
# fail the strict -Wall -Wextra build. obligraph is vendored, fixing its
# warnings is out of scope.
App_Include_Paths := -Icommon -Iapp -Iapp/core_logic -isystem obligraph/include

# Debug level setting
ifndef DEBUG_LEVEL
    DEBUG_LEVEL := 0
endif

# Compiler flags
App_Compile_CFlags := $(COMMON_FLAGS) -fPIC $(App_Include_Paths) -DDEBUG_LEVEL=$(DEBUG_LEVEL)
App_Compile_CXXFlags := $(App_Compile_CFlags) -std=c++20

# Add SLIM_ENTRY flag if specified
ifeq ($(SLIM_ENTRY), 1)
    App_Compile_CFlags += -DSLIM_ENTRY
    App_Compile_CXXFlags += -DSLIM_ENTRY
endif

# Link flags (just pthread, no SGX libraries)
App_Link_Flags := -lpthread

# All object files (routed through $(OBJ_PREFIX) for out-of-tree builds)
App_Objects := $(addprefix $(OBJ_PREFIX), $(App_Cpp_Files:.cpp=.o) $(App_C_Files:.c=.o))

App_Name := sgx_app

######## Build Targets ########

.PHONY: all build clean

all: $(App_Name)
	@echo "Build complete!"

build: all

# Memory-reduced build for the slim HI-Large E2 run. Recurses with a separate
# object tree (build_slim/) and a shrunken MAX_ATTRIBUTES so the default
# ./sgx_app and its objects are left completely untouched. Guarded by an empty
# BUILD_DIR so the recipe exists only at the top level — inside the recursive
# sub-make (BUILD_DIR=build_slim) `sgx_app_slim` is produced by the link rule
# instead, with no "overriding recipe" conflict.
ifeq ($(BUILD_DIR),)
.PHONY: sgx_app_slim
sgx_app_slim:
	@$(MAKE) BUILD_DIR=build_slim MAX_ATTRIBUTES=40 App_Name=sgx_app_slim
	@echo "Slim build complete: ./sgx_app_slim (MAX_ATTRIBUTES=40)"
endif

######## App Build ########

# Compile C++ source files. The $(OBJ_PREFIX) prefix routes objects into
# $(BUILD_DIR) for out-of-tree builds; with OBJ_PREFIX empty these reduce to the
# original in-tree rules. `mkdir -p $(@D)` is a no-op for the default build.
$(OBJ_PREFIX)main/%.o: main/%.cpp
	@mkdir -p $(@D)
	@$(CXX) $(App_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

$(OBJ_PREFIX)app/%.o: app/%.cpp
	@mkdir -p $(@D)
	@$(CXX) $(App_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

# Compile C++ source files from obligraph (parallel sort primitives)
$(OBJ_PREFIX)obligraph/src/%.o: obligraph/src/%.cpp
	@mkdir -p $(@D)
	@$(CXX) $(App_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

# Compile C source files from core_logic
$(OBJ_PREFIX)app/core_logic/%.o: app/core_logic/%.c
	@mkdir -p $(@D)
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
Test_Waksman_Objects := tests/unit/test_waksman_shuffle.o
Test_Waksman_Dist_Objects := tests/unit/test_waksman_distribution.o

# Common objects needed by test programs (reuse from main app)
Test_Common_Objects := app/file_io/converters.o \
                      app/file_io/table_io.o \
                      app/data_structures/table.o \
                      app/join/join_condition.o \
                      app/join/join_attribute_setter.o \
                      app/debug_stubs.o \
                      $(App_C_Files:.c=.o)

# Test executables
test_join: $(Test_Join_Objects) $(Test_Common_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

sqlite_baseline: $(Sqlite_Baseline_Objects) $(Test_Common_Objects) app/query/query_parser.o app/query/query_tokenizer.o app/query/inequality_parser.o app/query/condition_merger.o app/join/join_constraint.o app/join/join_condition.o
	@$(CXX) $^ -o $@ $(App_Link_Flags) -lsqlite3
	@echo "LINK =>  $@"

test_waksman_shuffle: $(Test_Waksman_Objects) $(Test_Common_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

test_waksman_distribution: $(Test_Waksman_Dist_Objects) $(Test_Common_Objects)
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
tests: test_join sqlite_baseline test_waksman_shuffle test_waksman_distribution
	@echo "All tests built successfully"

######## Clean ########

clean:
	@rm -f $(App_Name)
	@rm -f $(App_Objects)
	@rm -f sgx_app_slim
	@rm -rf build_slim
	@rm -f test_join sqlite_baseline test_waksman_shuffle test_waksman_distribution
	@rm -f $(Test_Join_Objects) $(Sqlite_Baseline_Objects) $(Test_Waksman_Objects) $(Test_Waksman_Dist_Objects)
	@rm -f app/core_logic/**/*.o