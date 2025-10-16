#pragma once

#include <atomic>
#include <thread>

namespace obligraph {
    // Declaration only - the actual definition is in config.cpp
    extern std::atomic<int> number_of_threads;

} // namespace obligraph
