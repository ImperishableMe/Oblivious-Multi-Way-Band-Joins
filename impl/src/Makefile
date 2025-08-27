#
# SGX Makefile for Memory Constrained Oblivious Multi-Way Join
# Based on Intel SGX SDK Sample
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
	SGX_ENCLAVE_SIGNER := $(SGX_SDK)/bin/x86/sgx_sign
	SGX_EDGER8R := $(SGX_SDK)/bin/x86/sgx_edger8r
else
	SGX_COMMON_FLAGS := -m64
	SGX_LIBRARY_PATH := $(SGX_SDK)/lib64
	SGX_ENCLAVE_SIGNER := $(SGX_SDK)/bin/x64/sgx_sign
	SGX_EDGER8R := $(SGX_SDK)/bin/x64/sgx_edger8r
endif

# Debug/Release flags
ifeq ($(SGX_DEBUG), 1)
ifeq ($(SGX_PRERELEASE), 1)
$(error Cannot set SGX_DEBUG and SGX_PRERELEASE at the same time!!)
endif
endif

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
SGX_COMMON_CFLAGS := $(SGX_COMMON_FLAGS) -Wjump-misses-init -Wstrict-prototypes
SGX_COMMON_CXXFLAGS := $(SGX_COMMON_FLAGS) -Wnon-virtual-dtor -std=c++11

######## App Settings ########

ifneq ($(SGX_MODE), HW)
	Urts_Library_Name := sgx_urts_sim
else
	Urts_Library_Name := sgx_urts
endif

# App source files
App_Cpp_Files := app/main.cpp \
                 app/crypto/crypto_utils.cpp \
                 app/io/converters.cpp \
                 app/data_structures/types.cpp \
                 app/debug_util.cpp
App_Include_Paths := -I$(SGX_SDK)/include -Iapp -Ienclave -Icommon

App_Compile_CFlags := -fPIC -Wno-attributes $(App_Include_Paths)

# Debug configuration
ifeq ($(SGX_DEBUG), 1)
	App_Compile_CFlags += -DDEBUG -UNDEBUG -UEDEBUG
else ifeq ($(SGX_PRERELEASE), 1)
	App_Compile_CFlags += -DNDEBUG -DEDEBUG -UDEBUG
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

# Generated untrusted source
Gen_Untrusted_Source := app/Enclave_u.c
Gen_Untrusted_Object := app/Enclave_u.o
App_Objects := $(App_Cpp_Files:.cpp=.o) $(Gen_Untrusted_Object)

App_Name := sgx_app
Encrypt_Tool := encrypt_tables

######## Enclave Settings ########

ifneq ($(SGX_MODE), HW)
	Trts_Library_Name := sgx_trts_sim
	Service_Library_Name := sgx_tservice_sim
else
	Trts_Library_Name := sgx_trts
	Service_Library_Name := sgx_tservice
endif
Crypto_Library_Name := sgx_tcrypto

# Enclave source files
Enclave_Cpp_Files := enclave/Enclave.cpp
Enclave_C_Files := enclave/crypto/entry_crypto.c \
                   enclave/crypto/aes_crypto.c \
                   enclave/core/crypto_helpers.c \
                   enclave/core/window_functions.c \
                   enclave/core/comparators.c \
                   enclave/core/transform_functions.c \
                   enclave/debug_wrapper.c

Enclave_Include_Paths := -I$(SGX_SDK)/include -I$(SGX_SDK)/include/tlibc \
                         -I$(SGX_SDK)/include/libcxx -Ienclave -Icommon

# Compiler version check for stack protector
CC_BELOW_4_9 := $(shell expr "`$(CC) -dumpversion`" \< "4.9")
ifeq ($(CC_BELOW_4_9), 1)
	Enclave_Compile_CFlags := -fstack-protector
else
	Enclave_Compile_CFlags := -fstack-protector-strong
endif

Enclave_Compile_CFlags += -nostdinc -ffreestanding -fvisibility=hidden -fpie \
                          -ffunction-sections -fdata-sections $(Enclave_Include_Paths) \
                          -DENCLAVE_BUILD
Enclave_Compile_CXXFlags := -nostdinc++ -std=c++11 $(Enclave_Compile_CFlags)

# Security flags
Enclave_Security_Link_Flags := -Wl,-z,relro,-z,now,-z,noexecstack

# Enclave linker flags
Enclave_Link_Flags := $(Enclave_Security_Link_Flags) \
    -Wl,--no-undefined -nostdlib -nodefaultlibs -nostartfiles -L$(SGX_LIBRARY_PATH) \
    -Wl,--whole-archive -l$(Trts_Library_Name) -Wl,--no-whole-archive \
    -Wl,--start-group -lsgx_tstdc -lsgx_tcxx -l$(Crypto_Library_Name) \
                      -l$(Service_Library_Name) -Wl,--end-group \
    -Wl,-Bstatic -Wl,-Bsymbolic -Wl,--no-undefined -Wl,-pie,-eenclave_entry \
    -Wl,--export-dynamic -Wl,--defsym,__ImageBase=0 -Wl,--gc-sections \
    -Wl,--version-script=enclave/Enclave.lds

# Generated trusted source
Gen_Trusted_Source := enclave/Enclave_t.c
Gen_Trusted_Object := enclave/Enclave_t.o

Enclave_Objects := $(Enclave_Cpp_Files:.cpp=.o) $(Enclave_C_Files:.c=.o) \
                  $(Gen_Trusted_Object)

Enclave_Name := enclave.so
Signed_Enclave_Name := enclave.signed.so
Enclave_Config_File := enclave/Enclave.config.xml
Enclave_Test_Key := enclave/Enclave_private_test.pem

######## Build Mode ########

ifeq ($(SGX_MODE), HW)
ifeq ($(SGX_DEBUG), 1)
	Build_Mode = HW_DEBUG
else ifeq ($(SGX_PRERELEASE), 1)
	Build_Mode = HW_PRERELEASE
else
	Build_Mode = HW_RELEASE
endif
else
ifeq ($(SGX_DEBUG), 1)
	Build_Mode = SIM_DEBUG
else ifeq ($(SGX_PRERELEASE), 1)
	Build_Mode = SIM_PRERELEASE
else
	Build_Mode = SIM_RELEASE
endif
endif

.PHONY: all target clean

all: .config_$(Build_Mode)_$(SGX_ARCH)
	@$(MAKE) target

ifeq ($(Build_Mode), HW_RELEASE)
target: $(App_Name) $(Enclave_Name) $(Encrypt_Tool)
	@echo "The project has been built in release hardware mode."
	@echo "Please sign the enclave first with your signing key."
else
target: $(App_Name) $(Signed_Enclave_Name) $(Encrypt_Tool)
	@echo "Build complete: $(Build_Mode) mode"
	@echo "Run with: ./$(App_Name)"
	@echo "Encrypt tables with: ./$(Encrypt_Tool)"
endif

.config_$(Build_Mode)_$(SGX_ARCH):
	@rm -f .config_* $(App_Name) $(Enclave_Name) $(Signed_Enclave_Name)
	@rm -f $(App_Objects) $(Enclave_Objects)
	@rm -f app/Enclave_u.* enclave/Enclave_t.*
	@touch .config_$(Build_Mode)_$(SGX_ARCH)

######## App Build ########

$(Gen_Untrusted_Source): $(SGX_EDGER8R) enclave/Enclave.edl
	@cd app && $(SGX_EDGER8R) --untrusted ../enclave/Enclave.edl \
		--search-path $(SGX_SDK)/include --search-path ../enclave
	@echo "GEN  =>  $@"

$(Gen_Untrusted_Object): $(Gen_Untrusted_Source)
	@$(CC) $(SGX_COMMON_CFLAGS) $(App_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

app/%.o: app/%.cpp $(Gen_Untrusted_Source)
	@$(CXX) $(SGX_COMMON_CXXFLAGS) $(App_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

$(App_Name): $(App_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

# Build encrypt_tables utility
Encrypt_Tool_Objects := app/tools/encrypt_tables.o app/io/table_io.o app/data_structures/join_constraint.o \
                       app/io/converters.o app/crypto/crypto_utils.o app/data_structures/types.o app/debug_util.o $(Gen_Untrusted_Object)

app/tools/encrypt_tables.o: app/tools/encrypt_tables.cpp $(Gen_Untrusted_Source)
	@$(CXX) $(SGX_COMMON_CXXFLAGS) $(App_Compile_CXXFlags) -std=c++17 -c $< -o $@
	@echo "CXX  <=  $<"

$(Encrypt_Tool): $(Encrypt_Tool_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags) -lstdc++fs
	@echo "LINK =>  $@"

######## Enclave Build ########

$(Gen_Trusted_Source): $(SGX_EDGER8R) enclave/Enclave.edl
	@cd enclave && $(SGX_EDGER8R) --trusted Enclave.edl \
		--search-path $(SGX_SDK)/include --search-path .
	@echo "GEN  =>  $@"

$(Gen_Trusted_Object): $(Gen_Trusted_Source)
	@$(CC) $(SGX_COMMON_CFLAGS) $(Enclave_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

enclave/%.o: enclave/%.cpp $(Gen_Trusted_Source)
	@$(CXX) $(SGX_COMMON_CXXFLAGS) $(Enclave_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

enclave/%.o: enclave/%.c
	@$(CC) $(SGX_COMMON_CFLAGS) $(Enclave_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

enclave/crypto/%.o: enclave/crypto/%.c
	@$(CC) $(SGX_COMMON_CFLAGS) $(Enclave_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

enclave/core/%.o: enclave/core/%.c
	@$(CC) $(SGX_COMMON_CFLAGS) $(Enclave_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

$(Enclave_Name): $(Enclave_Objects)
	@$(CXX) $^ -o $@ $(Enclave_Link_Flags)
	@echo "LINK =>  $@"

# Generate test signing key if it doesn't exist
$(Enclave_Test_Key):
	@openssl genrsa -out $@ -3 3072

$(Signed_Enclave_Name): $(Enclave_Name) $(Enclave_Config_File) $(Enclave_Test_Key)
	@$(SGX_ENCLAVE_SIGNER) sign -key $(Enclave_Test_Key) -enclave $(Enclave_Name) \
		-out $@ -config $(Enclave_Config_File)
	@echo "SIGN =>  $@"

######## Clean ########

clean:
	@rm -f .config_* $(App_Name) $(Enclave_Name) $(Signed_Enclave_Name) $(Encrypt_Tool)
	@rm -f $(App_Objects) $(Enclave_Objects) $(Encrypt_Tool_Objects)
	@rm -f app/Enclave_u.* enclave/Enclave_t.*
	@rm -f enclave/*.o enclave/crypto/*.o enclave/core/*.o app/*.o app/utils/*.o *.o

######## Help ########

help:
	@echo "Usage: make [target] [options]"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build the SGX application and enclave"
	@echo "  clean     - Remove all build artifacts"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Options:"
	@echo "  SGX_MODE=SIM       - Build in simulation mode (default: HW)"
	@echo "  SGX_DEBUG=1        - Build with debug symbols (default: 1)"
	@echo "  SGX_PRERELEASE=1   - Build in prerelease mode"
	@echo ""
	@echo "Examples:"
	@echo "  make                    - Build in hardware debug mode"
	@echo "  make SGX_MODE=SIM       - Build in simulation debug mode"
	@echo "  make clean              - Clean all build files"