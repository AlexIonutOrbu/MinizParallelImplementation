#pragma once

#include <charconv>
#include <cstdio>
#include <string>
#include <string_view>

#include "config.hpp"

inline void usage(const char* prog) {
    std::fprintf(stderr,
        R"(Uso: %s [opzioni] <file_o_dir> [altri_file_o_dir...] 
        dove le size di -s e -b possono essere in K (kilobyte) e M (megabyte)
Opzioni:
  -r 0|1     Recur into directories (default 1)
  -C 0|1     Compress (0 preserve, 1 remove (default 0)
  -D 0|1     Decompress (0 preserve, 1 remove (default 0)
  -t int     Numero di thread (default numero di core della macchina). Disponibile solo nella versione parallela
  -s size_t  Soglia per cui un file è considerato piccolo (default da decidere sperimentalmente). Disponibile solo nella versione parallela
  -b size_t  Dimensione dei blocchi paralleli (default da decidere sperimentalmente). Disponibile solo nella versione parallela
  -v         Verbose mode
  -h         Help

Esempio:
  %s -r 1 -C 0 -t 4 -s 2M -b 256K mydir file1.bin file2.txt
)",
        prog, prog);
}

inline long parseCommandLine(int argc, char* argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        return -1;
    }

    // safe convertion from char to int
    auto to_int = [](const char* s, int& out) -> bool {
        if (!s) return false;
        const char* begin = s;
        const char* end = s + std::char_traits<char>::length(s);
        auto r = std::from_chars(begin, end, out);
        return r.ec == std::errc{} && r.ptr == end;
    };
    // Converting size parameters, for example 2K = 1024 * 2
    auto parse_size = [](const std::string& str, size_t& val) -> bool {
        if (str.empty()) return false;
        try {
            size_t pos = 0;
            // on pos there will be the index of the first non numerical character
            size_t num = std::stoull(str, &pos);

            // Skips if there are no characters on the string
            if (pos < str.size()) {
                std::string suffix = str.substr(pos);  // Extracts the string after the number

                if (suffix == "K" || suffix == "k" || suffix == "KB" || suffix == "kb") {
                    num *= 1024;
                } else if (suffix == "M" || suffix == "m" || suffix == "MB" || suffix == "mb") {
                    num *= 1024 * 1024;
                } else {
                    return false;  // Wrongo suffix
                }
            }
            // If no problem occured
            val = num;
            return true;

        } catch (const std::exception&) {
            return false;
        }
    };
    bool seenC = false, seenD = false;
    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        std::string_view opt = argv[i];
        if (opt == "-h" || opt == "--help") {
            usage(argv[0]);
            return -1;
        } else if (opt == "-v") {
            cfg.verbose = true;
            ++i;
        } else if (opt == "-s" || opt == "-b") {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return -1;
            }
            size_t val = 0;
            if (!parse_size(argv[i + 1], val)) {
                usage(argv[0]);
                return -1;
            }
            opt == "-s" ? cfg.file_limit = val : cfg.block_size = val;
            i += 2;
        } else if (opt == "-r" || opt == "-C" || opt == "-D" || opt == "-t") {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return -1;
            }
            int val = 0;
            if (!to_int(argv[i + 1], val)) {
                usage(argv[0]);
                return -1;
            }

            if (opt == "-r") {
                cfg.recursive = (val != 0);
            } else if (opt == "-t") {
                cfg.num_threads = val;
            } else if (opt == "-C") {
                if (seenD) {
                    std::fprintf(stderr, "Error: only one between -C and -D.\n");
                    return -1;
                }
                seenC = true;
                cfg.mode = COMP;
                cfg.remove_input = (val != 0);
            } else {  // -D
                if (seenC) {
                    std::fprintf(stderr, "Error: only one between -C and -D.\n");
                    return -1;
                }
                seenD = true;
                cfg.mode = DECOMP;
                cfg.remove_input = (val != 0);
            }
            i += 2;
        } else {
            usage(argv[0]);
            return -1;
        }
    }

    if (!seenC && !seenD) {
        std::fprintf(stderr, "Error: at least one between -C and -D.\n");
        usage(argv[0]);
        return -1;
    }
    if (i >= argc) {
        std::fprintf(stderr, "Error: no file or dir to work.\n");
        usage(argv[0]);
        return -1;
    }
    cfg.pool = std::make_unique<threadPool>(cfg.num_threads);
    return i;  // index of the first optional args
}

// DEBUGGING

inline void print_info() {
    std::fprintf(stderr,
        R"(Parameters set:
Opzioni:
  -r %d
  remove input %d
  mode %d 
  -t %d
  -s %zu
  -b %zu
  -v %d                
)",
        cfg.recursive, cfg.remove_input, cfg.mode, cfg.num_threads, cfg.file_limit, cfg.block_size, cfg.verbose);
}