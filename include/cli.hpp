#pragma once

#include "types.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace wc {

struct CommonOptions {
    std::string input;
    std::string out;
    std::string stats;
    std::size_t top = 20;
    std::size_t buffer_bytes = 4u * 1024u * 1024u;
};

// Parses the flags shared by wc_seq and wc_par. Unknown flags are returned in
// `extra` so the parallel binary can parse its own flags from the same list.
inline bool parse_common(int argc, char** argv, CommonOptions& o,
                         std::vector<std::string>& extra, std::string& error) {
    extra.clear();
    bool have_input = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--top" || arg == "--out" || arg == "--stats" || arg == "--buffer-bytes") {
            if (i + 1 >= argc) {
                error = "missing value for " + arg;
                return false;
            }
            const std::string value = argv[++i];
            if (arg == "--top") {
                try {
                    const unsigned long long n = std::stoull(value);
                    if (n == 0 || n > 1000000ull) {
                        error = "--top must be between 1 and 1000000";
                        return false;
                    }
                    o.top = static_cast<std::size_t>(n);
                } catch (const std::exception&) {
                    error = "invalid --top value: " + value;
                    return false;
                }
            } else if (arg == "--buffer-bytes") {
                try {
                    const unsigned long long n = std::stoull(value);
                    if (n == 0 || n > (1ull << 34)) {
                        error = "--buffer-bytes must be between 1 and 17179869184";
                        return false;
                    }
                    o.buffer_bytes = static_cast<std::size_t>(n);
                } catch (const std::exception&) {
                    error = "invalid --buffer-bytes value: " + value;
                    return false;
                }
            } else if (arg == "--out") {
                o.out = value;
            } else {
                o.stats = value;
            }
        } else if (!arg.empty() && arg[0] == '-') {
            extra.push_back(arg);
        } else if (!have_input) {
            o.input = arg;
            have_input = true;
        } else {
            error = "unexpected positional argument: " + arg;
            return false;
        }
    }
    if (!have_input) {
        error = "missing input file";
        return false;
    }
    return true;
}

inline std::vector<Row> sorted_rows(const Counter& counter) {
    std::vector<Row> rows;
    rows.reserve(counter.size());
    for (const auto& entry : counter) {
        rows.push_back(Row{entry.first, entry.second});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.count != b.count) {
            return a.count > b.count;
        }
        return a.word < b.word;
    });
    return rows;
}

// Writes the FULL sorted table so wc_verify can compare implementations
// byte-for-byte. `--top` only controls what is printed to stdout.
inline void write_counts(const std::string& path, const std::vector<Row>& rows) {
    if (path.empty()) {
        return;
    }
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot write counts file '" + path + "'");
    }
    for (const Row& row : rows) {
        out << row.word << '\t' << row.count << '\n';
    }
}

inline void print_top(const std::vector<Row>& rows, std::size_t top) {
    const std::size_t n = std::min(top, rows.size());
    for (std::size_t i = 0; i < n; ++i) {
        std::cout << rows[i].word << '\t' << rows[i].count << '\n';
    }
}

} // namespace wc
