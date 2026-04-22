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
// TimingEntry: one recorded interval.
//
// `contributes_to_total` is false for diagnostic scopes that run inside a
// parallel block (per-branch sub-stages). Such entries still show in the
// breakdown but are NOT summed into category totals / TIMING_REPORTED, so
// the reported total is always the true wall-clock — otherwise parallel
// branches would be double-counted and the number would no longer reflect
// the benefit of concurrency.
// ---------------------------------------------------------------------------
struct TimingEntry {
    std::string name;
    std::string category;  // "IO", "OFFLINE", "ONLINE"
    double ms;
    bool contributes_to_total;
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

    void record(const std::string& name, const std::string& category, double ms,
                bool contributes_to_total = true) {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.push_back({name, category, ms, contributes_to_total});
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
    }

    // Sum of all wall-clock-contributing entries whose category is in `categories`.
    // Diagnostic entries (contributes_to_total=false) are excluded.
    double total(const std::vector<std::string>& categories) const {
        std::lock_guard<std::mutex> lock(mutex_);
        double sum = 0.0;
        for (const auto& e : entries_) {
            if (!e.contributes_to_total) continue;
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
        std::cout << "(* = diagnostic, inside a parallel block — NOT summed into totals)\n";
        std::string cur_cat;
        for (const auto& e : entries_) {
            if (e.category != cur_cat) {
                cur_cat = e.category;
                std::cout << "\n[" << cur_cat << "]\n";
            }
            std::cout << "  " << std::left << std::setw(name_w) << e.name
                      << (e.contributes_to_total ? "  " : " *")
                      << std::right << std::setw(9) << std::fixed << std::setprecision(2)
                      << e.ms << " ms\n";
        }

        // Per-category sums (wall-clock: diagnostic entries excluded)
        std::cout << "\n--- Category totals (wall-clock) ---\n";
        double grand = 0.0;
        for (const auto& cat : all_cats) {
            double sum = 0.0;
            for (const auto& e : entries_) {
                if (e.category == cat && e.contributes_to_total) sum += e.ms;
            }
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
            if (!e.contributes_to_total) continue;
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
    TimedScope(std::string name, std::string category,
               bool contributes_to_total = true)
        : name_(std::move(name)),
          category_(std::move(category)),
          contributes_to_total_(contributes_to_total),
          start_(std::chrono::high_resolution_clock::now()) {}

    ~TimedScope() {
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start_).count();
        TimingCollector::get().record(name_, category_, ms, contributes_to_total_);
    }

    TimedScope(const TimedScope&) = delete;
    TimedScope& operator=(const TimedScope&) = delete;

private:
    std::string name_;
    std::string category_;
    bool contributes_to_total_;
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
