#pragma once
#include <string>
#include <thread>
#include <threadPool.hpp>
// operations
enum WorkMode : int { COMP = 0,
    DECOMP = 1 };

// Global configuration arguments
struct Config {
    int recursive = 1;                                                    // -r 0|1 (default: recur)
    bool remove_input = false;                                            // -C 0|1 or -D 0|1, 1 = remove, 0 = preserve (default 0)
    int level = 6;                                                        // -l 0..9 compression level
    WorkMode mode = WorkMode::COMP;                                       // default: compress
    bool verbose = false;                                                 // -v verbose mode
    std::string out_ext = ".defl";                                        // estensione del file compresso
    size_t block_size = 1024;                                             // Placeholder momentaneo
    size_t file_limit = 1024;                                             // Placeholder momentaneo
    int num_threads = std::max(1u, std::thread::hardware_concurrency());  // numero di thread
    std::unique_ptr<threadPool> pool; // Sposta la gestione qui
    
};

// Unique instance of the global cfg variable (C++17 inline variable)
inline Config cfg;
