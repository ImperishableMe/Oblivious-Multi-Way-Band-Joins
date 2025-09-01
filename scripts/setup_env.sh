#!/bin/bash

# Environment setup and verification script for Oblivious Multi-Way Join project
# Checks prerequisites and sets up environment variables

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Status tracking
ERRORS=0
WARNINGS=0

print_header() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}  SGX Oblivious Join Environment Check  ${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo ""
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
    ERRORS=$((ERRORS + 1))
}

print_warning() {
    echo -e "${YELLOW}⚠${NC} $1"
    WARNINGS=$((WARNINGS + 1))
}

print_info() {
    echo -e "${BLUE}ℹ${NC} $1"
}

# Check OS
check_os() {
    echo -e "\n${BLUE}[Operating System]${NC}"
    
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        print_info "OS: $NAME $VERSION"
        
        if [[ "$ID" == "ubuntu" ]]; then
            print_success "Ubuntu detected"
            
            # Check Ubuntu version (20.04 or later recommended)
            VERSION_ID_MAJOR=$(echo $VERSION_ID | cut -d. -f1)
            if [ "$VERSION_ID_MAJOR" -ge 20 ]; then
                print_success "Ubuntu version $VERSION_ID (supported)"
            else
                print_warning "Ubuntu version $VERSION_ID (20.04 or later recommended)"
            fi
        else
            print_warning "Non-Ubuntu system detected. Ubuntu 20.04+ recommended"
        fi
    else
        print_error "Cannot determine OS version"
    fi
}

# Check SGX hardware support
check_sgx_hardware() {
    echo -e "\n${BLUE}[SGX Hardware Support]${NC}"
    
    if [ -f /proc/cpuinfo ]; then
        if grep -q sgx /proc/cpuinfo; then
            print_success "CPU supports SGX"
        else
            print_warning "SGX not detected in CPU flags (may still be supported)"
        fi
    fi
    
    if [ -e /dev/sgx_enclave ]; then
        print_success "SGX device /dev/sgx_enclave found"
    elif [ -e /dev/sgx ]; then
        print_success "SGX device /dev/sgx found"
    elif [ -e /dev/isgx ]; then
        print_success "SGX device /dev/isgx found (legacy)"
    else
        print_warning "No SGX device found (simulation mode will be used)"
    fi
}

# Check SGX SDK
check_sgx_sdk() {
    echo -e "\n${BLUE}[Intel SGX SDK]${NC}"
    
    if [ -n "$SGX_SDK" ]; then
        print_info "SGX_SDK set to: $SGX_SDK"
        
        if [ -d "$SGX_SDK" ]; then
            print_success "SGX SDK directory exists"
            
            # Check for key SDK components
            if [ -f "$SGX_SDK/bin/x64/sgx_edger8r" ]; then
                print_success "sgx_edger8r found"
            else
                print_error "sgx_edger8r not found in SDK"
            fi
            
            if [ -f "$SGX_SDK/bin/x64/sgx_sign" ]; then
                print_success "sgx_sign found"
            else
                print_error "sgx_sign not found in SDK"
            fi
        else
            print_error "SGX SDK directory not found at $SGX_SDK"
        fi
    else
        print_warning "SGX_SDK environment variable not set"
        
        # Try common locations
        COMMON_PATHS="/opt/intel/sgxsdk /opt/sgxsdk ~/sgxsdk"
        for path in $COMMON_PATHS; do
            if [ -d "$path" ]; then
                print_info "Found SGX SDK at: $path"
                print_info "Run: source $path/environment"
                break
            fi
        done
    fi
}

# Check SGX PSW (Platform Software)
check_sgx_psw() {
    echo -e "\n${BLUE}[Intel SGX PSW]${NC}"
    
    # Check AESM service
    if systemctl is-active --quiet aesmd; then
        print_success "AESM service is running"
    else
        if systemctl list-units --full -all | grep -Fq "aesmd.service"; then
            print_warning "AESM service installed but not running"
            print_info "Try: sudo systemctl start aesmd"
        else
            print_warning "AESM service not installed"
        fi
    fi
    
    # Check SGX libraries
    if ldconfig -p | grep -q libsgx; then
        print_success "SGX libraries found in system"
    else
        print_warning "SGX libraries not found in ldconfig"
    fi
}

# Check build tools
check_build_tools() {
    echo -e "\n${BLUE}[Build Tools]${NC}"
    
    # Check GCC
    if command -v gcc &> /dev/null; then
        GCC_VERSION=$(gcc --version | head -n1)
        print_success "GCC found: $GCC_VERSION"
    else
        print_error "GCC not found"
    fi
    
    # Check G++
    if command -v g++ &> /dev/null; then
        GPP_VERSION=$(g++ --version | head -n1)
        print_success "G++ found: $GPP_VERSION"
    else
        print_error "G++ not found"
    fi
    
    # Check Make
    if command -v make &> /dev/null; then
        MAKE_VERSION=$(make --version | head -n1)
        print_success "Make found: $MAKE_VERSION"
    else
        print_error "Make not found"
    fi
    
    # Check CMake (optional)
    if command -v cmake &> /dev/null; then
        CMAKE_VERSION=$(cmake --version | head -n1)
        print_success "CMake found: $CMAKE_VERSION"
    else
        print_info "CMake not found (optional)"
    fi
}

# Check Python (for test scripts)
check_python() {
    echo -e "\n${BLUE}[Python Environment]${NC}"
    
    if command -v python3 &> /dev/null; then
        PYTHON_VERSION=$(python3 --version)
        print_success "Python3 found: $PYTHON_VERSION"
    else
        print_warning "Python3 not found (needed for some test scripts)"
    fi
}

# Check project structure
check_project() {
    echo -e "\n${BLUE}[Project Structure]${NC}"
    
    SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
    PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"
    
    print_info "Project root: $PROJECT_ROOT"
    
    # Check key directories
    REQUIRED_DIRS="impl/src input scripts docs"
    for dir in $REQUIRED_DIRS; do
        if [ -d "$PROJECT_ROOT/$dir" ]; then
            print_success "Directory $dir exists"
        else
            print_error "Directory $dir not found"
        fi
    done
    
    # Check for test data
    if [ -d "$PROJECT_ROOT/input/plaintext/data_0_001" ]; then
        print_success "Test data found"
    else
        print_warning "Test data not found in input/plaintext/"
    fi
}

# Generate environment script
generate_env_script() {
    echo -e "\n${BLUE}[Environment Setup]${NC}"
    
    ENV_FILE="$HOME/.sgx_oblivious_join_env"
    
    cat > "$ENV_FILE" << 'EOF'
# SGX Oblivious Join Environment Setup
# Source this file to set up your environment

# SGX SDK location (adjust if needed)
export SGX_SDK=${SGX_SDK:-/opt/intel/sgxsdk}

# Add SGX SDK to PATH
export PATH=$PATH:$SGX_SDK/bin:$SGX_SDK/bin/x64

# Add SGX SDK libraries to LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$SGX_SDK/sdk_libs

# SGX Mode (HW or SIM)
export SGX_MODE=${SGX_MODE:-HW}

# Debug mode
export SGX_DEBUG=${SGX_DEBUG:-1}

echo "SGX environment configured:"
echo "  SGX_SDK: $SGX_SDK"
echo "  SGX_MODE: $SGX_MODE"
echo "  SGX_DEBUG: $SGX_DEBUG"
EOF
    
    print_success "Environment script created: $ENV_FILE"
    print_info "To use: source $ENV_FILE"
}

# Main execution
print_header

check_os
check_sgx_hardware
check_sgx_sdk
check_sgx_psw
check_build_tools
check_python
check_project

# Summary
echo -e "\n${BLUE}========================================${NC}"
echo -e "${BLUE}              Summary                   ${NC}"
echo -e "${BLUE}========================================${NC}"

if [ $ERRORS -eq 0 ]; then
    if [ $WARNINGS -eq 0 ]; then
        print_success "All checks passed! System is ready."
    else
        print_warning "System is ready with $WARNINGS warnings."
    fi
    
    generate_env_script
    
    echo -e "\n${GREEN}Next steps:${NC}"
    echo "1. Source the SGX environment:"
    echo "   source ~/.sgx_oblivious_join_env"
    echo "2. Build the project:"
    echo "   ./scripts/build.sh --all"
    echo "3. Run tests:"
    echo "   cd impl/src && ./test/test_join ../../input/queries/tpch_tb1.sql ../../input/encrypted/data_0_001"
else
    print_error "Found $ERRORS errors. Please fix them before building."
    
    echo -e "\n${RED}Required fixes:${NC}"
    if [ -z "$SGX_SDK" ]; then
        echo "- Install Intel SGX SDK and set SGX_SDK environment variable"
    fi
    if ! command -v gcc &> /dev/null; then
        echo "- Install GCC: sudo apt-get install build-essential"
    fi
fi

exit $ERRORS