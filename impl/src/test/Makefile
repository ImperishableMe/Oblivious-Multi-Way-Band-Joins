#
# Test Makefile for Memory Constrained Oblivious Join
#

######## SGX SDK Settings ########

SGX_SDK ?= /opt/intel/sgxsdk
SGX_MODE ?= HW
SGX_ARCH ?= x64
SGX_DEBUG ?= 1

# Detect 32-bit architecture
ifeq ($(shell getconf LONG_BIT), 32)
	SGX_ARCH := x86
else ifeq ($(findstring -m32, $(CXXFLAGS)), -m32)
	SGX_ARCH := x86
endif

# Architecture-specific settings
ifeq ($(SGX_ARCH), x86)
	SGX_COMMON_FLAGS := -m32
	SGX_LIBRARY_PATH := $(SGX_SDK)/lib
else
	SGX_COMMON_FLAGS := -m64
	SGX_LIBRARY_PATH := $(SGX_SDK)/lib64
endif

# Debug/Release flags
ifeq ($(SGX_DEBUG), 1)
	SGX_COMMON_FLAGS += -O0 -g
else
	SGX_COMMON_FLAGS += -O2
endif

# Warning flags
SGX_COMMON_FLAGS += -Wall -Wextra -Winit-self -Wpointer-arith -Wreturn-type \
                    -Waddress -Wsequence-point -Wformat-security \
                    -Wmissing-include-dirs -Wfloat-equal -Wundef -Wshadow \
                    -Wcast-align -Wconversion -Wredundant-decls
SGX_COMMON_CXXFLAGS := $(SGX_COMMON_FLAGS) -Wnon-virtual-dtor -std=c++11

######## App Settings ########

ifneq ($(SGX_MODE), HW)
	Urts_Library_Name := sgx_urts_sim
else
	Urts_Library_Name := sgx_urts
endif

App_Include_Paths := -I$(SGX_SDK)/include -I../app -I../enclave -I../common

App_Compile_CFlags := -fPIC -Wno-attributes $(App_Include_Paths)

# Slim entry mode support
ifdef SLIM_ENTRY
	App_Compile_CFlags += -DSLIM_ENTRY=$(SLIM_ENTRY)
endif

# Debug configuration
ifeq ($(SGX_DEBUG), 1)
	App_Compile_CFlags += -DDEBUG -UNDEBUG -UEDEBUG
else
	App_Compile_CFlags += -DNDEBUG -UEDEBUG -UDEBUG
endif

App_Compile_CXXFlags := $(App_Compile_CFlags) -std=c++11
App_Link_Flags := -L$(SGX_LIBRARY_PATH) -l$(Urts_Library_Name) -lpthread

ifneq ($(SGX_MODE), HW)
	App_Link_Flags += -lsgx_uae_service_sim
else
	App_Link_Flags += -lsgx_uae_service
endif

# Shared source files from app
Shared_Cpp_Files := ../app/io/table_io.cpp \
                   ../app/io/converters.cpp \
                   ../app/crypto/crypto_utils.cpp \
                   ../app/data_structures/entry.cpp \
                   ../app/data_structures/table.cpp \
                   ../app/data_structures/join_condition.cpp \
                   ../app/batch/ecall_batch_collector.cpp \
                   ../app/ecall_wrapper.cpp \
                   ../app/debug_util.cpp

# Generated untrusted source
Gen_Untrusted_Source := ../app/Enclave_u.c
Gen_Untrusted_Object := ../app/Enclave_u.o

# SQLite baseline
SQLite_Baseline := sqlite_baseline
SQLite_Objects := baseline/sqlite_baseline.o $(Shared_Cpp_Files:.cpp=.o) $(Gen_Untrusted_Object)
SQLite_Link_Flags := $(App_Link_Flags) -lsqlite3

# Test comparator
Test_Comparator := test_join
Test_Objects := compare/test_join.o $(Shared_Cpp_Files:.cpp=.o) $(Gen_Untrusted_Object)

Overhead_Test := overhead_measurement
Overhead_Objects := overhead_measurement.o $(Shared_Cpp_Files:.cpp=.o) $(Gen_Untrusted_Object)

Overhead_Test_V2 := overhead_measurement_v2
Overhead_V2_Objects := overhead_measurement_v2.o $(Shared_Cpp_Files:.cpp=.o) $(Gen_Untrusted_Object)

Crypto_Breakdown := overhead_crypto_breakdown
Crypto_Breakdown_Objects := overhead_crypto_breakdown.o $(Shared_Cpp_Files:.cpp=.o) $(Gen_Untrusted_Object)

.PHONY: all clean

all: $(SQLite_Baseline) $(Test_Comparator) $(Overhead_Test) $(Overhead_Test_V2) $(Crypto_Breakdown)

######## SQLite Baseline Build ########

baseline/%.o: baseline/%.cpp
	@$(CXX) $(SGX_COMMON_CXXFLAGS) $(App_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

$(SQLite_Baseline): $(SQLite_Objects)
	@$(CXX) $^ -o $@ $(SQLite_Link_Flags)
	@echo "LINK =>  $@"

######## Test Comparator Build ########

compare/%.o: compare/%.cpp
	@$(CXX) $(SGX_COMMON_CXXFLAGS) $(App_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

$(Test_Comparator): $(Test_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

######## Overhead Measurement Build ########

overhead_measurement.o: overhead_measurement.cpp
	@$(CXX) $(SGX_COMMON_CXXFLAGS) $(App_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

$(Overhead_Test): $(Overhead_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

######## Overhead V2 Build ########

overhead_measurement_v2.o: overhead_measurement_v2.cpp
	@$(CXX) $(SGX_COMMON_CXXFLAGS) $(App_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

$(Overhead_Test_V2): $(Overhead_V2_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

######## Crypto Breakdown Build ########

overhead_crypto_breakdown.o: overhead_crypto_breakdown.cpp
	@$(CXX) $(SGX_COMMON_CXXFLAGS) $(App_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

$(Crypto_Breakdown): $(Crypto_Breakdown_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

######## Common Build Rules ########

../app/%.o: ../app/%.cpp
	@$(CXX) $(SGX_COMMON_CXXFLAGS) $(App_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

$(Gen_Untrusted_Object): $(Gen_Untrusted_Source)
	@$(CC) $(SGX_COMMON_CFLAGS) $(App_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

######## Clean ########

clean:
	@rm -f $(SQLite_Baseline) $(Test_Comparator) $(Overhead_Test)
	@rm -f baseline/*.o compare/*.o overhead_measurement.o
	@rm -f ../app/*.o ../app/io/*.o ../app/crypto/*.o ../app/data_structures/*.o ../app/batch/*.o

######## Help ########

help:
	@echo "Usage: make [target] [options]"
	@echo ""
	@echo "Targets:"
	@echo "  all               - Build all test executables"
	@echo "  sqlite_baseline   - Build SQLite baseline only"
	@echo "  test_join        - Build test comparator only"
	@echo "  clean            - Remove all build artifacts"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Options:"
	@echo "  SGX_MODE=SIM     - Build in simulation mode (default: HW)"
	@echo "  SGX_DEBUG=1      - Build with debug symbols (default: 1)"
	@echo ""
	@echo "Examples:"
	@echo "  make                     - Build all test tools in HW debug mode"
	@echo "  make SGX_MODE=SIM        - Build in simulation mode"
	@echo "  make sqlite_baseline     - Build only the SQLite baseline"