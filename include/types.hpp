#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace wc {

using Count = std::uint64_t;
using Counter = std::unordered_map<std::string, Count>;

struct Timings {
    double setup = 0.0;       // delimiter-aligned boundary computation (parallel only)
    double parallel = 0.0;    // wall time of the OpenMP region (plus tree merge, if used)
    double read = 0.0;        // summed per-thread time spent reading chunks
    double count = 0.0;       // summed per-thread time spent tokenizing + counting
    double merge_wait = 0.0;  // summed per-thread time waiting to enter the critical section
    double merge_hold = 0.0;  // summed per-thread time inside the critical section (or tree merge wall)
    double topn = 0.0;        // sorting the global table for top-N / output
    double total = 0.0;       // wall time from start to sorted table, excludes file writing
};

struct Summary {
    std::string impl;
    std::string schedule;
    std::string merge_mode;
    int threads = 1;
    std::size_t num_chunks = 1;
    std::size_t buffer_bytes = 0;
    std::uint64_t size_bytes = 0;
    Count total_tokens = 0;
    std::size_t unique_words = 0;
    std::uint64_t peak_rss_kb = 0;
    Timings t;
};

struct Row {
    std::string word;
    Count count = 0;
};

} // namespace wc
