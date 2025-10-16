#pragma once

#include <chrono>
#include <string>
#include <iostream>
#include <functional>
#include <vector>
#include <cmath>

namespace obligraph {

/**
 * @brief RAII Timer class that measures execution time of a scope
 * 
 * This class follows RAII principles: it starts timing when constructed
 * and automatically prints the elapsed time when it goes out of scope.
 * 
 * Usage:
 *   {
 *     ScopeTimer timer("Database Operation");
 *     // ... code to time ...
 *   } // Timer destructor prints elapsed time here
 */
class ScopedTimer {
private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_time_;

public:
    /**
     * @brief Construct a new Scope Timer object
     * 
     * @param name The name/description of the operation being timed
     */
    explicit ScopedTimer(const std::string& name) 
        : name_(name), start_time_(std::chrono::high_resolution_clock::now()) {
    #ifdef OBL_DEBUG
        std::cout << "[TIMER] Starting: " << name_ << std::endl;
    #endif
    }

    /**
     * @brief Destroy the Scope Timer object
     * 
     * Automatically calculates and prints the elapsed time since construction
     */
    ~ScopedTimer() {
    #ifdef OBL_DEBUG
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time_
        );
        
        // Convert to more readable format
        double milliseconds = duration.count() / 1000.0;
        
        if (milliseconds < 1000.0) {
            std::cout << "[TIMER] Finished: " << name_ 
                      << " - Time taken: " << milliseconds << " ms" << std::endl;
        } else {
            double seconds = milliseconds / 1000.0;
            std::cout << "[TIMER] Finished: " << name_ 
                      << " - Time taken: " << seconds << " s" << std::endl;
        }
    #endif
    }

    // Delete copy constructor and assignment operator to prevent copying
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
};

/**
 * @brief RAII Benchmark class for running functions multiple times and calculating timing statistics
 * 
 * This class follows RAII principles: it starts benchmarking when constructed
 * and automatically prints timing statistics when it goes out of scope.
 * 
 * Usage:
 *   {
 *     Benchmark benchmark([]() { some_function(); }, 1000);
 *     // Benchmark runs automatically in constructor
 *   } // Statistics printed automatically in destructor
 */
class Benchmark {
private:
    std::function<void()> function_;
    int iterations_;
    std::vector<double> execution_times_;
    
public:
    /**
     * @brief Construct a new Benchmark object and immediately run the benchmark
     * 
     * @param func The function to benchmark (wrapped in std::function<void()>)
     * @param n Number of times to run the function
     */
    Benchmark(std::function<void()> func, int n) 
        : function_(func), iterations_(n) {
        execution_times_.reserve(n);
        run();
    }
    
    /**
     * @brief Destroy the Benchmark object
     * 
     * Automatically prints comprehensive timing statistics
     */
    ~Benchmark() {
        printResults();
    }
    
    /**
     * @brief Run the benchmark
     * 
     * Executes the function for the specified number of iterations and records timing data
     */
    void run() {
        execution_times_.clear();
        
        std::cout << "[BENCHMARK] Starting benchmark with " << iterations_ << " iterations..." << std::endl;
        
        for (int i = 0; i < iterations_; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            function_();
            auto end = std::chrono::high_resolution_clock::now();
            
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            double milliseconds = duration.count() / 1000.0;
            execution_times_.push_back(milliseconds);
        }
        
        std::cout << "[BENCHMARK] Completed " << iterations_ << " iterations" << std::endl;
    }
    
    /**
     * @brief Get the average execution time
     * 
     * @return double Average execution time in milliseconds
     */
    double getAverageTime() const {
        if (execution_times_.empty()) {
            throw std::runtime_error("No benchmark data available");
        }
        
        double sum = 0.0;
        for (double time : execution_times_) {
            sum += time;
        }
        return sum / execution_times_.size();
    }
    
    /**
     * @brief Get the standard deviation of execution times
     * 
     * @return double Standard deviation in milliseconds
     */
    double getStandardDeviation() const {
        if (execution_times_.empty()) {
            throw std::runtime_error("No benchmark data available");
        }
        
        double mean = getAverageTime();
        double sum_squared_diff = 0.0;
        
        for (double time : execution_times_) {
            double diff = time - mean;
            sum_squared_diff += diff * diff;
        }
        
        double variance = sum_squared_diff / execution_times_.size();
        return std::sqrt(variance);
    }
    
    /**
     * @brief Get the minimum execution time
     * 
     * @return double Minimum execution time in milliseconds
     */
    double getMinTime() const {
        if (execution_times_.empty()) {
            throw std::runtime_error("No benchmark data available");
        }
        
        return *std::min_element(execution_times_.begin(), execution_times_.end());
    }
    
    /**
     * @brief Get the maximum execution time
     * 
     * @return double Maximum execution time in milliseconds
     */
    double getMaxTime() const {
        if (execution_times_.empty()) {
            throw std::runtime_error("No benchmark data available");
        }
        
        return *std::max_element(execution_times_.begin(), execution_times_.end());
    }
    
    /**
     * @brief Print comprehensive benchmark results
     */
    void printResults() const {
        if (execution_times_.empty()) {
            std::cout << "[BENCHMARK] No results available." << std::endl;
            return;
        }
        
        std::cout << "[BENCHMARK] Results after " << iterations_ << " iterations:" << std::endl;
        std::cout << "  Average: " << getAverageTime() << " ms" << std::endl;
        std::cout << "  Std Dev: " << getStandardDeviation() << " ms" << std::endl;
        std::cout << "  Min:     " << getMinTime() << " ms" << std::endl;
        std::cout << "  Max:     " << getMaxTime() << " ms" << std::endl;
    }

    // Delete copy constructor and assignment operator to prevent copying
    Benchmark(const Benchmark&) = delete;
    Benchmark& operator=(const Benchmark&) = delete;
};

} // namespace obligraph
