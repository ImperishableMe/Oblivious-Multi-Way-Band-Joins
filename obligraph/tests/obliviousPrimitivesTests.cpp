#include <gtest/gtest.h>
#include <vector>
#include <algorithm>
#include <random>
#include <iostream>
#include <unordered_map>

#include "../include/obl_building_blocks.h"
#include "../include/config.h"

using namespace obligraph;

class ObliviousCompactTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up random number generator for reproducible tests
        gen.seed(42);
        pool = std::make_unique<ThreadPool>(obligraph::number_of_threads);
    }

    void TearDown() override {
        pool.reset();
    }

    // Helper function to count elements with each tag
    std::pair<int, int> countTags(const std::vector<uint8_t>& tags) {
        int ones = 0, zeros = 0;
        for (uint8_t tag : tags) {
            if (tag == 1) ones++;
            else if (tag == 0) zeros++;
        }
        return {ones, zeros};
    }

    template<typename T>
    bool verifyCompactness(std::vector<T>& original_data, const std::vector<uint8_t>& original_tags,
                        std::vector<T>& compacted_data) {
        // auto [ones, zeros] = countTags(original_tags);
        std::vector <T> vecs[2];
        for (int i = 0; i < original_data.size(); i++) {
            vecs[original_tags[i]].push_back(original_data[i]);
        }
        // Verify that the compacted data matches the expected layout
        if (vecs[0].size() + vecs[1].size() != compacted_data.size()) return false;
        sort(vecs[0].begin(), vecs[0].end());
        sort(vecs[1].begin(), vecs[1].end());
        int one_count = vecs[1].size();
        sort(compacted_data.begin(), compacted_data.begin() + one_count);
        sort( compacted_data.begin() + vecs[1].size(), compacted_data.end());

        for (size_t i = 0; i < vecs[1].size(); i++) {
            if (vecs[1][i] != compacted_data[i]) return false;
        }
        for (size_t i = 0; i < vecs[0].size(); i++) {
            if (vecs[0][i] != compacted_data[vecs[1].size() + i]) return false;
        }
        return true;
    }

    // Helper function to verify that all original elements are still present
    template<typename T>
    bool verifyElementPreservation(const std::vector<T>& original, const std::vector<T>& compacted) {
        std::vector<T> orig_sorted = original;
        std::vector<T> comp_sorted = compacted;
        std::sort(orig_sorted.begin(), orig_sorted.end());
        std::sort(comp_sorted.begin(), comp_sorted.end());
        return orig_sorted == comp_sorted;
    }

    std::mt19937 gen;
    std::unique_ptr<ThreadPool> pool;
};

TEST_F(ObliviousCompactTest, EmptyArray) {
    std::vector<int> data;
    std::vector<uint8_t> tags;
    
    // Should handle empty arrays gracefully
    ASSERT_NO_THROW({
        parallel_o_compact(data.begin(), data.end(), *pool, tags.data(), 1);
    });
}

TEST_F(ObliviousCompactTest, SingleElement) {
    // Test with single element tagged as 1
    std::vector<int> data1 = {42};
    auto data2 = data1;
    std::vector<uint8_t> tags1 = {1};
    
    parallel_o_compact(data1.begin(), data1.end(), *pool, tags1.data(), 1);
    
    EXPECT_EQ(data1[0], 42);
    EXPECT_EQ(tags1[0], 1);
    EXPECT_TRUE(verifyCompactness(data2, tags1, data1));
}

TEST_F(ObliviousCompactTest, TwoElements_AlreadyCompacted) {
    // Test [1-tagged, 0-tagged] - should remain unchanged
    std::vector<int> data = {10, 20};
    std::vector<uint8_t> tags = {1, 0};
    std::vector<int> original_data = data;
    auto original_tags = tags;
    
    parallel_o_compact(data.begin(), data.end(), *pool, tags.data(), 1);
    
    EXPECT_TRUE(verifyCompactness(original_data, original_tags, data));
    EXPECT_TRUE(verifyElementPreservation(original_data, data));
}

TEST_F(ObliviousCompactTest, TwoElements_NeedsCompacting) {
    // Test [0-tagged, 1-tagged] - should be swapped
    std::vector<int> data = {10, 20};
    std::vector<uint8_t> tags = {0, 1};
    std::vector<int> original_data = data;
    
    parallel_o_compact(data.begin(), data.end(), *pool, tags.data(), 1);
    
    EXPECT_TRUE(verifyCompactness(original_data, tags, data));
    EXPECT_TRUE(verifyElementPreservation(original_data, data));
    EXPECT_EQ(data[0], 20);  // Element with tag 1 should come first
    EXPECT_EQ(data[1], 10);  // Element with tag 0 should come second
}

TEST_F(ObliviousCompactTest, AllOnes) {
    std::vector<int> data = {1, 2, 3, 4, 5};
    std::vector<uint8_t> tags = {1, 1, 1, 1, 1};
    std::vector<int> original_data = data;
    
    parallel_o_compact(data.begin(), data.end(), *pool, tags.data(), 1);

    EXPECT_TRUE(verifyCompactness(original_data, tags, data));
    EXPECT_TRUE(verifyElementPreservation(original_data, data));
}

TEST_F(ObliviousCompactTest, AllZeros) {
    std::vector<int> data = {1, 2, 3, 4, 5};
    std::vector<uint8_t> tags = {0, 0, 0, 0, 0};
    std::vector<int> original_data = data;
    
    parallel_o_compact(data.begin(), data.end(), *pool, tags.data(), 1);

    EXPECT_TRUE(verifyCompactness(original_data, tags, data));
    EXPECT_TRUE(verifyElementPreservation(original_data, data));
}

TEST_F(ObliviousCompactTest, SmallMixedArray) {
    // Test with a small mixed array: [0, 1, 0, 1, 0]
    std::vector<int> data = {10, 20, 30, 40, 50};
    std::vector<uint8_t> tags = {0, 1, 0, 1, 0};
    std::vector<int> original_data = data;
    std::vector<uint8_t> original_tags = tags;
    auto original_counts = countTags(tags);
    
    parallel_o_compact(data.begin(), data.end(), *pool, tags.data(), 1);
    
    EXPECT_TRUE(verifyCompactness(original_data, original_tags, data));
    EXPECT_TRUE(verifyElementPreservation(original_data, data));
    
}

TEST_F(ObliviousCompactTest, PowerOfTwoSizes) {
    // Test with various power-of-two sizes
    for (int size_exp = 1; size_exp <= 6; size_exp++) {
        int size = 1 << size_exp;  // 2, 4, 8, 16, 32, 64
        
        std::vector<int> data(size);
        std::vector<uint8_t> tags(size);
        
        // Fill with alternating pattern
        for (int i = 0; i < size; i++) {
            data[i] = i + 1;
            tags[i] = (i % 2);  // Alternating 0, 1, 0, 1, ...
        }
        
        std::vector<int> original_data = data;
        auto original_tags = tags;
        auto original_counts = countTags(tags);
        
        parallel_o_compact(data.begin(), data.end(), *pool, tags.data(), 1);

        EXPECT_TRUE(verifyCompactness(original_data, original_tags, data)) << "Failed for size " << size;
        EXPECT_TRUE(verifyElementPreservation(original_data, data)) << "Failed for size " << size;
    }
}

TEST_F(ObliviousCompactTest, RandomizedTesting) {
    // Test with various random configurations
    std::uniform_int_distribution<int> size_dist(3, 50);
    std::uniform_int_distribution<int> value_dist(1, 1000);
    std::uniform_int_distribution<int> tag_dist(0, 1);
    
    for (int test = 0; test < 20; test++) {
        int size = size_dist(gen);
        std::vector<int> data(size);
        std::vector<uint8_t> tags(size);
        
        for (int i = 0; i < size; i++) {
            data[i] = value_dist(gen);
            tags[i] = tag_dist(gen);
        }
        
        std::vector<int> original_data = data;
        std::vector<uint8_t> original_tags = tags;
        auto original_counts = countTags(tags);
        
        parallel_o_compact(data.begin(), data.end(), *pool, tags.data(), 1);

        EXPECT_TRUE(verifyCompactness(original_data, original_tags, data)) 
            << "Failed for random test " << test << " with size " << size;
        EXPECT_TRUE(verifyElementPreservation(original_data, data)) 
            << "Failed for random test " << test << " with size " << size;
        
    }
}

TEST_F(ObliviousCompactTest, DifferentDataTypes) {
    // Test with different data types
    
    // Double
    std::vector<double> double_data = {1.5, 2.5, 3.5, 4.5};
    std::vector<uint8_t> double_tags = {0, 1, 0, 1};
    std::vector<double> original_double = double_data;
    std::vector<uint8_t> original_double_tags = double_tags;
    
    parallel_o_compact(double_data.begin(), double_data.end(), *pool, double_tags.data(), 1);
    
    EXPECT_TRUE(verifyCompactness(original_double, original_double_tags, double_data));
    EXPECT_TRUE(verifyElementPreservation(original_double, double_data));
    
    // Character
    std::vector<char> char_data = {'a', 'b', 'c', 'd', 'e'};
    std::vector<uint8_t> char_tags = {1, 0, 1, 0, 1};
    std::vector<char> original_char = char_data;
    std::vector<uint8_t> original_char_tags = char_tags;
    
    parallel_o_compact(char_data.begin(), char_data.end(), *pool, char_tags.data(), 1);
    
    EXPECT_TRUE(verifyCompactness(original_char, original_char_tags, char_data));
    EXPECT_TRUE(verifyElementPreservation(original_char, char_data));
    
    // Long long
    std::vector<long long> ll_data = {100LL, 200LL, 300LL};
    std::vector<uint8_t> ll_tags = {0, 0, 1};
    std::vector<long long> original_ll = ll_data;
    std::vector<uint8_t> original_ll_tags = ll_tags;
    
    parallel_o_compact(ll_data.begin(), ll_data.end(), *pool, ll_tags.data(), 1);
    
    EXPECT_TRUE(verifyCompactness(original_ll, original_ll_tags, ll_data));
    EXPECT_TRUE(verifyElementPreservation(original_ll, ll_data));
}

TEST_F(ObliviousCompactTest, LargeArray) {
    // Test with a larger array to ensure scalability
    const int size = 10000;
    std::vector<int> data(size);
    std::vector<uint8_t> tags(size);
    
    // Create a pattern where every 3rd element has tag 1
    for (int i = 0; i < size; i++) {
        data[i] = i + 1;
        tags[i] = (i % 3 == 0) ? 1 : 0;
    }
    
    std::vector<int> original_data = data;
    std::vector<uint8_t> original_tags = tags;

    pool = std::make_unique<ThreadPool>(12);

    parallel_o_compact(data.begin(), data.end(), *pool, tags.data(), 1);

    EXPECT_TRUE(verifyCompactness(original_data, original_tags, data));
    EXPECT_TRUE(verifyElementPreservation(original_data, data));
    
}

// Test edge cases
TEST_F(ObliviousCompactTest, EdgeCases) {
    // Test with maximum and minimum values
    std::vector<int> data = {INT_MAX, INT_MIN, 0, -1, 1};
    std::vector<uint8_t> tags = {1, 0, 1, 0, 1};
    std::vector<int> original_data = data;
    std::vector<uint8_t> original_tags = tags;
    
    parallel_o_compact(data.begin(), data.end(), *pool, tags.data(), 1);
    
    EXPECT_TRUE(verifyCompactness(original_data, original_tags, data));
    EXPECT_TRUE(verifyElementPreservation(original_data, data));
}
