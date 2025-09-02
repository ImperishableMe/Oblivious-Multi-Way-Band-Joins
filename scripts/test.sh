#!/bin/bash

# Unified Test Runner for Oblivious Multi-Way Join Project
# Usage: ./test.sh [command] [options]
#
# Commands:
#   quick           Run quick tests (TB1, TB2 on small data)
#   all             Run all tests on default dataset
#   selected        Run selected tests (wrapper for run_selected_tests.sh)
#   sequential      Run sequential tests (wrapper for run_sequential_tests.sh)
#   tpch           Run all TPC-H tests (wrapper for run_tpch_tests.sh)
#   single          Run a single test
#   help            Show this help message

# Get script directory and project root
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"

# Configuration
SRC_DIR="$PROJECT_ROOT/impl/src"
TEST_PROG="$SRC_DIR/test/test_join"
DEFAULT_SCALE="0_001"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Function to print colored messages
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Function to print usage
print_usage() {
    echo "Unified Test Runner for Oblivious Multi-Way Join Project"
    echo ""
    echo "Usage: $0 [command] [options]"
    echo ""
    echo "Commands:"
    echo "  quick                    Run quick tests (TB1, TB2 on small data)"
    echo "  all [scale]              Run all tests on specified scale (default: 0_001)"
    echo "  selected <args>          Run selected tests (see run_selected_tests.sh)"
    echo "  sequential <args>        Run sequential tests (see run_sequential_tests.sh)"
    echo "  tpch [scale]            Run all TPC-H tests (see run_tpch_tests.sh)"
    echo "  single <query> <scale>   Run a single test"
    echo "  help                     Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 quick                        # Quick sanity check"
    echo "  $0 all                          # Run all tests on default scale"
    echo "  $0 all 0_01                     # Run all tests on scale 0.01"
    echo "  $0 single tb1 0_001             # Run TB1 on scale 0.001"
    echo "  $0 selected 0_001 tb1 tb2       # Run selected tests"
    echo "  $0 sequential 0_001 0_01        # Run sequential tests on multiple scales"
    echo ""
    echo "Available data scales:"
    ls -d "$PROJECT_ROOT/input/encrypted/data_"* 2>/dev/null | xargs -n1 basename | sed 's/data_/  - /'
}

# Function to check prerequisites
check_prerequisites() {
    # Check if test_join exists
    if [ ! -f "$TEST_PROG" ]; then
        print_error "test_join not found at $TEST_PROG"
        print_warning "Please build first: $SCRIPT_DIR/build.sh --test"
        return 1
    fi
    
    # Check if data exists
    if [ ! -d "$PROJECT_ROOT/input/encrypted" ]; then
        print_error "Encrypted data directory not found"
        print_warning "Please ensure test data is available in input/encrypted/"
        return 1
    fi
    
    return 0
}

# Function to run quick tests
run_quick_tests() {
    print_info "Running quick tests..."
    
    local queries=("tpch_tb1" "tpch_tb2")
    local scale="0_001"
    local passed=0
    local failed=0
    
    for query in "${queries[@]}"; do
        print_info "Testing $query on data_$scale..."
        
        local query_file="$PROJECT_ROOT/input/queries/${query}.sql"
        local data_dir="$PROJECT_ROOT/input/encrypted/data_$scale"
        
        if $TEST_PROG "$query_file" "$data_dir" 2>&1 | grep -q "Match: YES"; then
            print_success "$query PASSED"
            passed=$((passed + 1))
        else
            print_error "$query FAILED"
            failed=$((failed + 1))
        fi
    done
    
    echo ""
    print_info "Quick test summary: $passed passed, $failed failed"
    
    if [ $failed -eq 0 ]; then
        print_success "All quick tests passed!"
        return 0
    else
        print_error "Some tests failed"
        return 1
    fi
}

# Function to run all tests
run_all_tests() {
    local scale="${1:-$DEFAULT_SCALE}"
    print_info "Running all tests on scale $scale..."
    
    "$SCRIPT_DIR/run_tpch_tests.sh" "$scale"
    return $?
}

# Function to run a single test
run_single_test() {
    if [ $# -lt 2 ]; then
        print_error "Usage: $0 single <query> <scale>"
        echo "Example: $0 single tb1 0_001"
        return 1
    fi
    
    local query="$1"
    local scale="$2"
    
    # Normalize query name
    if [[ ! "$query" =~ ^tpch_ ]]; then
        query="tpch_$query"
    fi
    
    local query_file="$PROJECT_ROOT/input/queries/${query}.sql"
    local data_dir="$PROJECT_ROOT/input/encrypted/data_$scale"
    
    if [ ! -f "$query_file" ]; then
        print_error "Query file not found: $query_file"
        return 1
    fi
    
    if [ ! -d "$data_dir" ]; then
        print_error "Data directory not found: $data_dir"
        return 1
    fi
    
    print_info "Running $query on data_$scale..."
    
    $TEST_PROG "$query_file" "$data_dir"
    local result=$?
    
    if [ $result -eq 0 ]; then
        print_success "Test completed"
    else
        print_error "Test failed with code $result"
    fi
    
    return $result
}

# Main execution
main() {
    # Check prerequisites first
    if ! check_prerequisites; then
        exit 1
    fi
    
    # Parse command
    local command="${1:-help}"
    shift
    
    case "$command" in
        quick)
            run_quick_tests
            ;;
        all)
            run_all_tests "$@"
            ;;
        selected)
            "$SCRIPT_DIR/run_selected_tests.sh" "$@"
            ;;
        sequential)
            "$SCRIPT_DIR/run_sequential_tests.sh" "$@"
            ;;
        tpch)
            "$SCRIPT_DIR/run_tpch_tests.sh" "$@"
            ;;
        single)
            run_single_test "$@"
            ;;
        help|--help|-h)
            print_usage
            exit 0
            ;;
        *)
            print_error "Unknown command: $command"
            echo ""
            print_usage
            exit 1
            ;;
    esac
}

# Run main function
main "$@"