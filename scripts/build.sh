#!/bin/bash

# Unified build script for Oblivious Multi-Way Join project
# Can be run from any directory
# Usage: ./build.sh [options]
#   Options:
#     --clean       Clean before building
#     --debug       Build with debug symbols
#     --slim        Build with slim entry mode
#     --test        Build test utilities
#     --all         Build everything (main + tests)
#     --help        Show this help message

# Get the script's directory (scripts/)
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
# Get project root (parent of scripts/)
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"
# Source directory
SRC_DIR="$PROJECT_ROOT/impl/src"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Parse command line arguments
CLEAN=0
DEBUG=0
SLIM=0
BUILD_TESTS=0
BUILD_ALL=0
SHOW_HELP=0

for arg in "$@"; do
    case $arg in
        --clean)
            CLEAN=1
            shift
            ;;
        --debug)
            DEBUG=1
            shift
            ;;
        --slim)
            SLIM=1
            shift
            ;;
        --test)
            BUILD_TESTS=1
            shift
            ;;
        --all)
            BUILD_ALL=1
            shift
            ;;
        --help)
            SHOW_HELP=1
            shift
            ;;
        *)
            echo -e "${RED}Unknown option: $arg${NC}"
            SHOW_HELP=1
            shift
            ;;
    esac
done

# Show help if requested
if [ $SHOW_HELP -eq 1 ]; then
    echo "Unified build script for Oblivious Multi-Way Join project"
    echo ""
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --clean       Clean before building"
    echo "  --debug       Build with debug symbols"
    echo "  --slim        Build with slim entry mode"
    echo "  --test        Build test utilities only"
    echo "  --all         Build everything (main + tests)"
    echo "  --help        Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                    # Standard build"
    echo "  $0 --clean --debug    # Clean debug build"
    echo "  $0 --slim             # Slim entry mode build"
    echo "  $0 --all              # Build everything"
    exit 0
fi

# Function to print colored status
print_status() {
    echo -e "${GREEN}[BUILD]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Check if we're in SGX environment
check_sgx() {
    if [ -z "$SGX_SDK" ]; then
        print_warning "SGX_SDK environment variable not set"
        print_warning "Trying default location: /opt/intel/sgxsdk"
        export SGX_SDK=/opt/intel/sgxsdk
    fi
    
    if [ ! -d "$SGX_SDK" ]; then
        print_error "SGX SDK not found at $SGX_SDK"
        exit 1
    fi
    
    print_status "Using SGX SDK at: $SGX_SDK"
}

# Main build function
build_main() {
    print_status "Building main application..."
    
    cd "$SRC_DIR" || {
        print_error "Cannot access source directory: $SRC_DIR"
        exit 1
    }
    
    # Prepare make command
    MAKE_CMD="make"
    
    if [ $CLEAN -eq 1 ]; then
        print_status "Cleaning previous build..."
        make clean > /dev/null 2>&1
    fi
    
    if [ $DEBUG -eq 1 ]; then
        print_status "Building in DEBUG mode..."
        MAKE_CMD="DEBUG=1 $MAKE_CMD"
    fi
    
    if [ $SLIM -eq 1 ]; then
        print_status "Building with SLIM_ENTRY mode..."
        MAKE_CMD="SLIM_ENTRY=1 $MAKE_CMD"
    fi
    
    # Execute build
    eval $MAKE_CMD
    
    if [ $? -eq 0 ]; then
        print_status "Main build successful!"
        print_status "Binaries created:"
        ls -lh sgx_app encrypt_tables 2>/dev/null | awk '{print "  - " $NF ": " $5}'
    else
        print_error "Main build failed!"
        exit 1
    fi
}

# Test build function
build_tests() {
    print_status "Building test utilities..."
    
    cd "$SRC_DIR/test" || {
        print_error "Cannot access test directory: $SRC_DIR/test"
        exit 1
    }
    
    if [ $CLEAN -eq 1 ]; then
        print_status "Cleaning test build..."
        make clean > /dev/null 2>&1
    fi
    
    # Build tests
    make
    
    if [ $? -eq 0 ]; then
        print_status "Test build successful!"
        print_status "Test binaries created:"
        ls -lh test_join sqlite_baseline overhead_* 2>/dev/null | awk '{print "  - " $NF ": " $5}'
    else
        print_error "Test build failed!"
        exit 1
    fi
}

# Main execution
print_status "Starting build process..."
print_status "Project root: $PROJECT_ROOT"

# Check SGX environment
check_sgx

# Determine what to build
if [ $BUILD_ALL -eq 1 ]; then
    build_main
    build_tests
elif [ $BUILD_TESTS -eq 1 ]; then
    build_tests
else
    # Default: build main only
    build_main
fi

print_status "Build complete!"

# Show usage hints
echo ""
echo "To run the application:"
echo "  cd $SRC_DIR"
echo "  ./sgx_app <query.sql> <encrypted_data_dir> <output.csv>"
echo ""
echo "To run tests:"
echo "  cd $SRC_DIR"
echo "  ./test/test_join <query.sql> <encrypted_data_dir>"