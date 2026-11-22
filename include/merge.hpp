#pragma once

#include "types.hpp"

#include <utility>
#include <vector>

namespace wc {

// Add all entries of `src` into `dst`. Called either from the critical
// section or from a tree-reduction round.
inline void add_into(Counter& dst, const Counter& src) {
    if (src.empty()) {
        return;
    }
    dst.reserve(dst.size() + src.size());
    for (const auto& entry : src) {
        dst[entry.first] += entry.second;
    }
}

// Pairwise parallel reduction over thread-local tables.
// log2(T) rounds; the larger table is kept as the merge destination.
inline Counter merge_tree(std::vector<Counter>& parts) {
    if (parts.empty()) {
        return {};
    }
    while (parts.size() > 1) {
        const std::size_t n = parts.size();
        const std::size_t pairs = (n + 1) / 2;
        #pragma omp parallel for schedule(static)
        for (std::size_t i = 0; i < pairs; ++i) {
            const std::size_t a = 2 * i;
            const std::size_t b = a + 1;
            if (b >= n) {
                continue;
            }
            if (parts[b].size() > parts[a].size()) {
                parts[a].swap(parts[b]);
            }
            add_into(parts[a], parts[b]);
            parts[b].clear();
            parts[b].rehash(0);
        }
        std::vector<Counter> next;
        next.reserve(pairs);
        for (std::size_t i = 0; i < n; i += 2) {
            next.push_back(std::move(parts[i]));
        }
        parts = std::move(next);
    }
    return std::move(parts[0]);
}

} // namespace wc
