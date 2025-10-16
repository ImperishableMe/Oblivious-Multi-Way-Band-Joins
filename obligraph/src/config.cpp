#include "../include/config.h"
#include <thread>

namespace obligraph {
    // Define the global variable - this is the single definition for the entire program
    std::atomic<int> number_of_threads(std::thread::hardware_concurrency());
} // namespace obligraph
