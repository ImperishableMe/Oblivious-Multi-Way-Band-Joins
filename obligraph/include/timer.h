#pragma once

#include <chrono>
#include <string>
#include <iostream>
#include <iomanip>
#include <functional>
#include <vector>
#include <cmath>
#include <mutex>
#include <numeric>
#include <algorithm>

namespace obligraph {

// ---------------------------------------------------------------------------
// TimingEntry: one recorded interval
// ---------------------------------------------------------------------------
struct TimingEntry {
    std::string name;
    std::string category;  // "IO", "OFFLINE", "ONLINE"
    double ms;
};

// ---------------------------------------------------------------------------
// TimingCollector: thread-safe singleton that accumulates all intervals.
//
// Usage:
//   TimingCollector::get().record("probe src", "ONLINE", 850.1);
//   TimingCollector::get().report({"ONLINE"});   // print + sum selected cats
//   TimingCollector::get().reset();
// ---------------------------------------------------------------------------
class TimingCollector {
public:
    static TimingCollector& get() {
        static TimingCollector instance;
        return instance;
    }

    void record(const std::string& name, const std::string& category, double ms) {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.push_back({name, category, ms});
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
    }

    // Sum of all entries whose category is in `categories`.
    double total(const std::vector<std::string>& categories) const {
        std::lock_guard<std::mutex> lock(mutex_);
        double sum = 0.0;
        for (const auto& e : entries_) {
            for (const auto& c : categories) {
                if (e.category == c) { sum += e.ms; break; }
            }
        }
        return sum;
    }

    // Print a full breakdown and emit one TIMING_REPORTED line for the
    // categories requested (empty = all categories).
    void report(const std::vector<std::string>& report_categories = {}) const {
        std::lock_guard<std::mutex> lock(mutex_);

        // Determine which categories exist
        std::vector<std::string> all_cats;
        for (const auto& e : entries_) {
            bool found = false;
            for (const auto& c : all_cats) if (c == e.category) { found = true; break; }
            if (!found) all_cats.push_back(e.category);
        }

        // Column width
        size_t max_name = 8;
        for (const auto& e : entries_) max_name = std::max(max_name, e.name.size());
        const size_t name_w = max_name + 2;

        std::cout << "\n=== TIMING BREAKDOWN ===\n";
        std::string cur_cat;
        for (const auto& e : entries_) {
            if (e.category != cur_cat) {
                cur_cat = e.category;
                std::cout << "\n[" << cur_cat << "]\n";
            }
            std::cout << "  " << std::left << std::setw(name_w) << e.name
                      << std::right << std::setw(9) << std::fixed << std::setprecision(2)
                      << e.ms << " ms\n";
        }

        // Per-category sums
        std::cout << "\n--- Category totals ---\n";
        double grand = 0.0;
        for (const auto& cat : all_cats) {
            double sum = 0.0;
            for (const auto& e : entries_) if (e.category == cat) sum += e.ms;
            grand += sum;
            std::cout << "  " << std::left << std::setw(10) << cat
                      << std::right << std::setw(9) << std::fixed << std::setprecision(2)
                      << sum << " ms\n";
        }
        std::cout << "  " << std::left << std::setw(10) << "GRAND"
                  << std::right << std::setw(9) << std::fixed << std::setprecision(2)
                  << grand << " ms\n";

        // Reported total (for grepping by test harness)
        const std::vector<std::string>& cats =
            report_categories.empty() ? all_cats : report_categories;
        double reported = 0.0;
        for (const auto& e : entries_) {
            for (const auto& c : cats) {
                if (e.category == c) { reported += e.ms; break; }
            }
        }
        std::string cats_str;
        for (size_t i = 0; i < cats.size(); i++) {
            if (i) cats_str += "+";
            cats_str += cats[i];
        }
        std::cout << "TIMING_REPORTED categories=" << cats_str
                  << " total=" << std::fixed << std::setprecision(3)
                  << reported << "ms\n";
    }

private:
    mutable std::mutex mutex_;
    std::vector<TimingEntry> entries_;

    TimingCollector() = default;
    TimingCollector(const TimingCollector&) = delete;
    TimingCollector& operator=(const TimingCollector&) = delete;
};

// ---------------------------------------------------------------------------
// TimedScope: RAII — always records to TimingCollector on destruction.
// ---------------------------------------------------------------------------
class TimedScope {
public:
    TimedScope(std::string name, std::string category)
        : name_(std::move(name)),
          category_(std::move(category)),
          start_(std::chrono::high_resolution_clock::now()) {}

    ~TimedScope() {
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start_).count();
        TimingCollector::get().record(name_, category_, ms);
    }

    TimedScope(const TimedScope&) = delete;
    TimedScope& operator=(const TimedScope&) = delete;

private:
    std::string name_;
    std::string category_;
    std::chrono::high_resolution_clock::time_point start_;
};

// ---------------------------------------------------------------------------
// Benchmark: unchanged from original.
// ---------------------------------------------------------------------------
class Benchmark {
public:
    Benchmark(std::function<void()> func, int n)
        : function_(func), iterations_(n) {
        execution_times_.reserve(n);
        run();
    }

    ~Benchmark() { printResults(); }

    void run() {
        execution_times_.clear();
        std::cout << "[BENCHMARK] Starting benchmark with " << iterations_ << " iterations...\n";
        for (int i = 0; i < iterations_; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            function_();
            auto end = std::chrono::high_resolution_clock::now();
            execution_times_.push_back(
                std::chrono::duration<double, std::milli>(end - start).count());
        }
        std::cout << "[BENCHMARK] Completed " << iterations_ << " iterations\n";
    }

    double getAverageTime() const {
        return std::accumulate(execution_times_.begin(), execution_times_.end(), 0.0)
               / execution_times_.size();
    }
    double getStandardDeviation() const {
        double mean = getAverageTime();
        double sq = 0.0;
        for (double t : execution_times_) sq += (t - mean) * (t - mean);
        return std::sqrt(sq / execution_times_.size());
    }
    double getMinTime() const {
        return *std::min_element(execution_times_.begin(), execution_times_.end());
    }
    double getMaxTime() const {
        return *std::max_element(execution_times_.begin(), execution_times_.end());
    }
    void printResults() const {
        std::cout << "[BENCHMARK] Results after " << iterations_ << " iterations:\n"
                  << "  Average: " << getAverageTime()       << " ms\n"
                  << "  Std Dev: " << getStandardDeviation() << " ms\n"
                  << "  Min:     " << getMinTime()           << " ms\n"
                  << "  Max:     " << getMaxTime()           << " ms\n";
    }

    Benchmark(const Benchmark&) = delete;
    Benchmark& operator=(const Benchmark&) = delete;

private:
    std::function<void()> function_;
    int iterations_;
    std::vector<double> execution_times_;
};

} // namespace obligraph
