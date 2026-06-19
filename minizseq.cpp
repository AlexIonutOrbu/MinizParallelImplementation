/*
 * miniz source code: https://github.com/richgel999/miniz
 * https://code.google.com/archive/p/miniz/
 *
 * This is a reworked version of the example3.c file distributed with the miniz.c.
 * --------------------
 * example3.c - Demonstrates how to use miniz.c's deflate() and inflate() functions for simple file compression.
 * Public domain, May 15 2011, Rich Geldreich, richgel99@gmail.com. See "unlicense" statement at the end of tinfl.c.
 * For simplicity, this example is limited to files smaller than 4GB, but this is not a limitation of miniz.c.
 * -------------------
 *
 */

#include <chrono>
#include <cstdio>
#include <iostream>

#include "cmdline.hpp"
#include "config.hpp"
#include "utility_seq.hpp"
using clock_type = std::chrono::steady_clock;

int main(int argc, char* argv[]) {
    // parse command line arguments and set some global variables
    long start = parseCommandLine(argc, argv);
    if (start < 0) return -1;
    auto t0 = clock_type::now();
    bool success = true;
    while (argv[start]) {
        if (isDirectory(argv[start])) {
            success &= walkDir(argv[start]);
        } else {
            success &= doWork(argv[start]);
        }
        start++;
    }
    if (!success) {
        printf("Exiting with (some) Error(s)\n");
        return -1;
    }
    printf("Exiting with Success\n");
    auto t1 = clock_type::now();
    double tsecSeq = std::chrono::duration<double>(t1 - t0).count();
    std::cout.setf(std::ios::fixed);
    std::cout.precision(6);
    std::cout << "sequential time = " << tsecSeq << " (s)\n";
    return 0;
}