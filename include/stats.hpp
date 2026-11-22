#pragma once

#include "types.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace wc {

inline double now() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

// Peak resident set size of this process, in KiB.
inline std::uint64_t peak_rss_kb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return static_cast<std::uint64_t>(counters.PeakWorkingSetSize / 1024ull);
    }
    return 0;
#else
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmHWM:", 0) == 0) {
            std::istringstream iss(line.substr(6));
            std::uint64_t kb = 0;
            if (iss >> kb) {
                return kb;
            }
        }
    }
    return 0;
#endif
}

inline std::string json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    for (const char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

inline void write_stats(const std::string& path, const Summary& s) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot write stats file '" + path + "'");
    }
    out << "{\n"
        << "  \"impl\": \"" << json_escape(s.impl) << "\",\n"
        << "  \"schedule\": \"" << json_escape(s.schedule) << "\",\n"
        << "  \"merge\": \"" << json_escape(s.merge_mode) << "\",\n"
        << "  \"threads\": " << s.threads << ",\n"
        << "  \"num_chunks\": " << s.num_chunks << ",\n"
        << "  \"buffer_bytes\": " << s.buffer_bytes << ",\n"
        << "  \"size_bytes\": " << s.size_bytes << ",\n"
        << "  \"total_tokens\": " << s.total_tokens << ",\n"
        << "  \"unique_words\": " << s.unique_words << ",\n"
        << "  \"peak_rss_kb\": " << s.peak_rss_kb << ",\n"
        << "  \"t_setup\": " << s.t.setup << ",\n"
        << "  \"t_parallel\": " << s.t.parallel << ",\n"
        << "  \"t_read\": " << s.t.read << ",\n"
        << "  \"t_count\": " << s.t.count << ",\n"
        << "  \"t_merge_wait\": " << s.t.merge_wait << ",\n"
        << "  \"t_merge_hold\": " << s.t.merge_hold << ",\n"
        << "  \"t_topn\": " << s.t.topn << ",\n"
        << "  \"t_total\": " << s.t.total << "\n"
        << "}\n";
}

} // namespace wc
