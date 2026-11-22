#include "cli.hpp"
#include "io.hpp"
#include "merge.hpp"
#include "stats.hpp"
#include "tokenizer.hpp"
#include "types.hpp"

#include <omp.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct ParOptions {
    int threads = omp_get_max_threads();
    std::string schedule = "static";
    std::size_t num_chunks = 0;  // 0 => T chunks for static, 8*T for dynamic
    std::string merge = "critical";
};

void usage() {
    std::cerr << "usage: wc_par <file> [--threads T] [--schedule static|dynamic] "
                 "[--num-chunks K] [--merge critical|tree] [--top N] [--out FILE] "
                 "[--stats FILE] [--buffer-bytes N]\n";
}

unsigned long long parse_positive(const std::string& value, const std::string& flag) {
    try {
        const unsigned long long n = std::stoull(value);
        if (n == 0 || n > 1000000ull) {
            throw std::runtime_error(flag + " must be between 1 and 1000000");
        }
        return n;
    } catch (const std::exception&) {
        throw std::runtime_error("invalid " + flag + " value: " + value);
    }
}

bool parse_parallel(const std::vector<std::string>& extra, ParOptions& o, std::string& error) {
    for (std::size_t i = 0; i < extra.size(); ++i) {
        const std::string& flag = extra[i];
        if (flag != "--threads" && flag != "--schedule" &&
            flag != "--num-chunks" && flag != "--merge") {
            error = "unknown flag: " + flag;
            return false;
        }
        if (i + 1 >= extra.size()) {
            error = "missing value for " + flag;
            return false;
        }
        const std::string value = extra[++i];
        if (flag == "--threads") {
            const auto n = parse_positive(value, flag);
            if (n > 256) {
                error = "--threads must be <= 256";
                return false;
            }
            o.threads = static_cast<int>(n);
        } else if (flag == "--num-chunks") {
            o.num_chunks = static_cast<std::size_t>(parse_positive(value, flag));
        } else if (flag == "--schedule") {
            o.schedule = value;
            if (o.schedule != "static" && o.schedule != "dynamic") {
                error = "--schedule must be static or dynamic";
                return false;
            }
        } else {
            o.merge = value;
            if (o.merge != "critical" && o.merge != "tree") {
                error = "--merge must be critical or tree";
                return false;
            }
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    try {
        wc::CommonOptions common;
        std::vector<std::string> extra;
        std::string error;
        if (!wc::parse_common(argc, argv, common, extra, error)) {
            std::cerr << "error: " << error << '\n';
            usage();
            return 2;
        }
        ParOptions par;
        if (!parse_parallel(extra, par, error)) {
            std::cerr << "error: " << error << '\n';
            usage();
            return 2;
        }

        const int requested_threads = par.threads;
        const std::size_t requested_chunks =
            par.num_chunks > 0
                ? par.num_chunks
                : (par.schedule == "dynamic"
                       ? static_cast<std::size_t>(8) * static_cast<std::size_t>(requested_threads)
                       : static_cast<std::size_t>(requested_threads));

        wc::Summary summary;
        summary.impl = "parallel";
        summary.schedule = par.schedule;
        summary.merge_mode = par.merge;
        summary.threads = requested_threads;
        summary.num_chunks = requested_chunks;
        summary.buffer_bytes = common.buffer_bytes;
        summary.size_bytes = wc::file_size(common.input);

        const double start = wc::now();
        const double setup_begin = wc::now();
        const std::vector<std::uint64_t> bounds =
            wc::compute_boundaries(common.input, summary.size_bytes, requested_chunks);
        summary.t.setup = wc::now() - setup_begin;
        const int chunk_count = static_cast<int>(bounds.size()) - 1;
        summary.num_chunks = static_cast<std::size_t>(chunk_count);

        std::vector<wc::Counter> locals(static_cast<std::size_t>(requested_threads));
        std::vector<double> read_acc(requested_threads, 0.0);
        std::vector<double> count_acc(requested_threads, 0.0);
        std::vector<double> wait_acc(requested_threads, 0.0);
        std::vector<double> hold_acc(requested_threads, 0.0);
        std::vector<wc::Count> token_acc(requested_threads, 0);
        std::vector<int> actual_threads(requested_threads, 0);
        wc::Counter global;
        global.reserve(1u << 16);

        const double parallel_begin = wc::now();
        #pragma omp parallel num_threads(requested_threads)
        {
            const int tid = omp_get_thread_num();
            actual_threads[static_cast<std::size_t>(tid)] = omp_get_num_threads();
            wc::Counter& local = locals[static_cast<std::size_t>(tid)];
            local.reserve(1u << 16);
            wc::Tokenizer tokenizer(local);

            const auto process_chunk = [&](int ci) {
                wc::ChunkReader reader(common.input, bounds[static_cast<std::size_t>(ci)],
                                       bounds[static_cast<std::size_t>(ci + 1)],
                                       common.buffer_bytes);
                const char* data = nullptr;
                std::size_t len = 0;
                while (true) {
                    const double read_begin = wc::now();
                    const bool ok = reader.next(data, len);
                    read_acc[static_cast<std::size_t>(tid)] += wc::now() - read_begin;
                    if (!ok) {
                        break;
                    }
                    const double count_begin = wc::now();
                    tokenizer.feed(data, len);
                    count_acc[static_cast<std::size_t>(tid)] += wc::now() - count_begin;
                }
            };

            if (par.schedule == "dynamic") {
                #pragma omp for schedule(dynamic)
                for (int ci = 0; ci < chunk_count; ++ci) {
                    process_chunk(ci);
                }
            } else {
                #pragma omp for schedule(static)
                for (int ci = 0; ci < chunk_count; ++ci) {
                    process_chunk(ci);
                }
            }

            const double finish_begin = wc::now();
            tokenizer.finish();
            count_acc[static_cast<std::size_t>(tid)] += wc::now() - finish_begin;
            token_acc[static_cast<std::size_t>(tid)] = tokenizer.tokens();

            if (par.merge == "critical") {
                const double wait_begin = wc::now();
                double entered = wait_begin;
                double left = wait_begin;
                #pragma omp critical(wc_merge)
                {
                    entered = wc::now();
                    wc::add_into(global, local);
                    left = wc::now();
                }
                wait_acc[static_cast<std::size_t>(tid)] += entered - wait_begin;
                hold_acc[static_cast<std::size_t>(tid)] += left - entered;
            }
        }
        double parallel_wall = wc::now() - parallel_begin;

        if (par.merge == "tree") {
            const double merge_begin = wc::now();
            global = wc::merge_tree(locals);
            const double merge_elapsed = wc::now() - merge_begin;
            hold_acc[0] += merge_elapsed;
            parallel_wall += merge_elapsed;
        }
        summary.t.parallel = parallel_wall;

        summary.total_tokens =
            std::accumulate(token_acc.begin(), token_acc.end(), wc::Count{0});
        summary.unique_words = global.size();
        summary.threads =
            *std::max_element(actual_threads.begin(), actual_threads.end());
        for (int t = 0; t < requested_threads; ++t) {
            const std::size_t i = static_cast<std::size_t>(t);
            summary.t.read += read_acc[i];
            summary.t.count += count_acc[i];
            summary.t.merge_wait += wait_acc[i];
            summary.t.merge_hold += hold_acc[i];
        }

        const double topn_begin = wc::now();
        const std::vector<wc::Row> rows = wc::sorted_rows(global);
        summary.t.topn = wc::now() - topn_begin;
        summary.t.total = wc::now() - start;
        summary.peak_rss_kb = wc::peak_rss_kb();

        wc::write_counts(common.out, rows);
        wc::print_top(rows, common.top);
        if (!common.stats.empty()) {
            wc::write_stats(common.stats, summary);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
