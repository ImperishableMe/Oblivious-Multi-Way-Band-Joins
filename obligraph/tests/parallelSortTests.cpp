#include <gtest/gtest.h>
#include "../include/obl_building_blocks.h"
#include <vector>
#include <algorithm>
#include <random>
#include <cstring>

using namespace obligraph;

class ParallelSortTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup test data
        rng.seed(42); // Fixed seed for reproducible tests
    }

    std::mt19937 rng;
    
    // Helper function to create test data
    template<typename T>
    std::vector<T> createRandomVector(size_t size, T min_val, T max_val) {
        std::vector<T> vec(size);
        std::uniform_int_distribution<T> dist(min_val, max_val);
        
        for (size_t i = 0; i < size; i++) {
            vec[i] = dist(rng);
        }
        return vec;
    }
    
    // Helper function to verify sorting
    template<typename T>
    bool isSorted(const std::vector<T>& vec) {
        return std::is_sorted(vec.begin(), vec.end());
    }
    
    template<typename T, typename Compare>
    bool isSorted(const std::vector<T>& vec, Compare comp) {
        return std::is_sorted(vec.begin(), vec.end(), comp);
    }
};

// Test empty vector
TEST_F(ParallelSortTest, EmptyVector) {
    ThreadPool pool(4);
    std::vector<int> vec;
    parallel_sort(vec.begin(), vec.end(), pool, std::less<int>(), 4);
    EXPECT_TRUE(vec.empty());
}

// Test single element vector
TEST_F(ParallelSortTest, SingleElement) {
    ThreadPool pool(4);
    std::vector<int> vec = {42};
    parallel_sort(vec.begin(), vec.end(), pool, std::less<int>(), 4);
    EXPECT_EQ(vec.size(), 1);
    EXPECT_EQ(vec[0], 42);
}

// Test two elements
TEST_F(ParallelSortTest, TwoElements) {
    ThreadPool pool(4);
    std::vector<int> vec = {2, 1};
    parallel_sort(vec.begin(), vec.end(), pool, std::less<int>(), 4);
    EXPECT_TRUE(isSorted(vec));
    EXPECT_EQ(vec[0], 1);
    EXPECT_EQ(vec[1], 2);
}

// Test already sorted vector
TEST_F(ParallelSortTest, AlreadySorted) {
    ThreadPool pool(4);
    std::vector<int> vec = {1, 2, 3, 4, 5, 6, 7, 8};
    parallel_sort(vec.begin(), vec.end(), pool, std::less<int>(), 4);
    EXPECT_TRUE(isSorted(vec));
    
    std::vector<int> expected = {1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT_EQ(vec, expected);
}

// Test reverse sorted vector
TEST_F(ParallelSortTest, ReverseSorted) {
    ThreadPool pool(4);
    std::vector<int> vec = {8, 7, 6, 5, 4, 3, 2, 1};
    parallel_sort(vec.begin(), vec.end(), pool, std::less<int>(), 4);
    EXPECT_TRUE(isSorted(vec));
    
    std::vector<int> expected = {1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT_EQ(vec, expected);
}

// Test with duplicates
TEST_F(ParallelSortTest, WithDuplicates) {
    ThreadPool pool(4);
    std::vector<int> vec = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3};
    parallel_sort(vec.begin(), vec.end(), pool, std::less<int>(), 4);
    EXPECT_TRUE(isSorted(vec));
    
    std::vector<int> expected = {1, 1, 2, 3, 3, 4, 5, 5, 6, 9};
    EXPECT_EQ(vec, expected);
}

// Test random data
TEST_F(ParallelSortTest, RandomData) {
    ThreadPool pool(4);
    for (size_t size : {8, 16, 32, 64}) {
        std::vector<int> vec = createRandomVector<int>(size, 1, 1000);
        std::vector<int> reference = vec;
        
        // Sort using our implementation
        parallel_sort(vec.begin(), vec.end(), pool, std::less<int>(), 4);
        
        // Sort using standard library for comparison
        std::sort(reference.begin(), reference.end());
        
        EXPECT_TRUE(isSorted(vec)) << "Vector of size " << size << " is not sorted";
        EXPECT_EQ(vec, reference) << "Vector of size " << size << " doesn't match reference";
    }
}

// Test with custom comparator (descending order)
TEST_F(ParallelSortTest, CustomComparator) {
    ThreadPool pool(4);
    std::vector<int> vec = {1, 2, 3, 4, 5, 6, 7, 8};
    parallel_sort(vec.begin(), vec.end(), pool, std::greater<int>(), 4);
    EXPECT_TRUE(isSorted(vec, std::greater<int>()));
    
    std::vector<int> expected = {8, 7, 6, 5, 4, 3, 2, 1};
    EXPECT_EQ(vec, expected);
}

// Test with different data types
TEST_F(ParallelSortTest, FloatData) {
    ThreadPool pool(4);
    std::vector<float> vec = {3.14f, 2.71f, 1.41f, 1.73f};
    parallel_sort(vec.begin(), vec.end(), pool, std::less<float>(), 4);
    EXPECT_TRUE(isSorted(vec));
    
    std::vector<float> expected = {1.41f, 1.73f, 2.71f, 3.14f};
    EXPECT_EQ(vec, expected);
}

// Test edge case: power of 2 sizes
TEST_F(ParallelSortTest, PowerOfTwoSizes) {
    ThreadPool pool(4);
    for (size_t exp = 0; exp <= 6; exp++) {
        size_t size = 1 << exp; // 1, 2, 4, 8, 16, 32, 64
        std::vector<int> vec = createRandomVector<int>(size, 1, 100);
        std::vector<int> reference = vec;
        
        parallel_sort(vec.begin(), vec.end(), pool, std::less<int>(), 4);
        std::sort(reference.begin(), reference.end());
        
        EXPECT_TRUE(isSorted(vec)) << "Power-of-2 vector of size " << size << " is not sorted";
        EXPECT_EQ(vec, reference) << "Power-of-2 vector of size " << size << " doesn't match reference";
    }
}

// Test edge case: non-power of 2 sizes
TEST_F(ParallelSortTest, NonPowerOfTwoSizes) {
    ThreadPool pool(4);
    for (size_t size : {3, 5, 7, 9, 15, 17, 31, 33, 63, 65}) {
        std::vector<int> vec = createRandomVector<int>(size, 1, 100);
        std::vector<int> reference = vec;
        
        parallel_sort(vec.begin(), vec.end(), pool, std::less<int>(), 4);
        std::sort(reference.begin(), reference.end());
        
        EXPECT_TRUE(isSorted(vec)) << "Non-power-of-2 vector of size " << size << " is not sorted";
        EXPECT_EQ(vec, reference) << "Non-power-of-2 vector of size " << size << " doesn't match reference";
    }
}

// Test with fixed-size struct
TEST_F(ParallelSortTest, FixedSizeStruct) {
    // Define a simple fixed-size struct
    struct Point {
        int x, y;
        
        Point() : x(0), y(0) {}
        Point(int x_val, int y_val) : x(x_val), y(y_val) {}
        
        // Equality operator for testing
        bool operator==(const Point& other) const {
            return x == other.x && y == other.y;
        }
    };
    
    // Custom comparator that sorts by x-coordinate first, then y-coordinate
    auto pointComparator = [](const Point& a, const Point& b) {
        if (a.x != b.x) {
            return a.x < b.x;
        }
        return a.y < b.y;
    };
    
    // Create test data
    std::vector<Point> vec = {
        Point(3, 4),
        Point(1, 2),
        Point(3, 1),
        Point(2, 3),
        Point(1, 5),
        Point(2, 1)
    };
    
    // Sort using our implementation
    ThreadPool pool(4);
    parallel_sort(vec.begin(), vec.end(), pool, pointComparator, 4);
    
    // Expected result: sorted by x, then by y
    std::vector<Point> expected = {
        Point(1, 2),
        Point(1, 5),
        Point(2, 1),
        Point(2, 3),
        Point(3, 1),
        Point(3, 4)
    };
    
    // Verify sorting
    EXPECT_TRUE(std::is_sorted(vec.begin(), vec.end(), pointComparator));
    EXPECT_EQ(vec, expected);
}

// Test with larger fixed-size struct and different sorting criteria
TEST_F(ParallelSortTest, LargerStructSorting) {
    // Define a more complex struct
    struct Employee {
        int id;
        int age;
        double salary;
        char department; // A, B, C, etc.
        char name[16];   // Fixed array of chars for employee name
        
        Employee() : id(0), age(0), salary(0.0), department('A') {
            std::strcpy(name, "Unknown");
        }
        Employee(int id_val, int age_val, double salary_val, char dept, const char* name_str)
            : id(id_val), age(age_val), salary(salary_val), department(dept) {
            std::strncpy(name, name_str, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0'; // Ensure null termination
        }
        
        bool operator==(const Employee& other) const {
            return id == other.id && age == other.age && 
                   salary == other.salary && department == other.department &&
                   std::strcmp(name, other.name) == 0;
        }
    };
    
    // Sort by salary (descending), then by age (ascending)
    auto salaryComparator = [](const Employee& a, const Employee& b) {
        if (a.salary != b.salary) {
            return a.salary > b.salary; // Descending salary
        }
        return a.age < b.age; // Ascending age for same salary
    };
    
    std::vector<Employee> vec = {
        Employee(1, 30, 50000.0, 'A', "Alice"),
        Employee(2, 25, 60000.0, 'B', "Bob"),
        Employee(3, 35, 50000.0, 'A', "Charlie"),
        Employee(4, 28, 60000.0, 'C', "Diana"),
        Employee(5, 40, 45000.0, 'B', "Eve")
    };
    
    // Create reference for comparison
    std::vector<Employee> reference = vec;
    
    // Sort using our implementation
    ThreadPool pool(4);
    parallel_sort(vec.begin(), vec.end(), pool, salaryComparator, 4);
    
    // Sort reference using standard library
    std::sort(reference.begin(), reference.end(), salaryComparator);
    
    // Verify sorting
    EXPECT_TRUE(std::is_sorted(vec.begin(), vec.end(), salaryComparator));
    EXPECT_EQ(vec, reference);
    
    // Also verify specific order
    std::vector<Employee> expected = {
        Employee(2, 25, 60000.0, 'B', "Bob"),    // Higher salary, younger
        Employee(4, 28, 60000.0, 'C', "Diana"),  // Higher salary, older
        Employee(1, 30, 50000.0, 'A', "Alice"),  // Lower salary, younger
        Employee(3, 35, 50000.0, 'A', "Charlie"), // Lower salary, older
        Employee(5, 40, 45000.0, 'B', "Eve")     // Lowest salary
    };
    
    EXPECT_EQ(vec, expected);
}
