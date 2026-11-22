#include "cli.hpp"
#include "io.hpp"
#include "stats.hpp"
#include "tokenizer.hpp"
#include "types.hpp"

#include <exception>
#include <iostream>
#include <vector>

namespace {

void usage() {
    std::cerr << "usage: wc_seq <file> [--top N] [--out FILE] [--stats FILE] "
                 "[--buffer-bytes N]\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        wc::CommonOptions opts;
        std::vector<std::string> extra;
        std::string error;
        if (!wc::parse_common(argc, argv, opts, extra, error) || !extra.empty()) {
            std::cerr << "error: " << (error.empty() ? "unknown flag: " + extra.front() : error)
                      << '\n';
            usage();
            return 2;
        }

        wc::Summary summary;
        summary.impl = "sequential";
        summary.schedule = "none";
        summary.merge_mode = "none";
        summary.threads = 1;
        summary.num_chunks = 1;
        summary.buffer_bytes = opts.buffer_bytes;
        summary.size_bytes = wc::file_size(opts.input);

        const double start = wc::now();

        wc::Counter counter;
        counter.reserve(1u << 16);
        wc::Tokenizer tokenizer(counter);
        wc::ChunkReader reader(opts.input, 0, summary.size_bytes, opts.buffer_bytes);

        const char* data = nullptr;
        std::size_t len = 0;
        while (true) {
            const double read_begin = wc::now();
            const bool ok = reader.next(data, len);
            summary.t.read += wc::now() - read_begin;
            if (!ok) {
                break;
            }
            const double count_begin = wc::now();
            tokenizer.feed(data, len);
            summary.t.count += wc::now() - count_begin;
        }
        const double finish_begin = wc::now();
        tokenizer.finish();
        summary.t.count += wc::now() - finish_begin;

        const double topn_begin = wc::now();
        const std::vector<wc::Row> rows = wc::sorted_rows(counter);
        summary.t.topn = wc::now() - topn_begin;
        summary.t.total = wc::now() - start;

        summary.total_tokens = tokenizer.tokens();
        summary.unique_words = counter.size();
        summary.peak_rss_kb = wc::peak_rss_kb();

        wc::write_counts(opts.out, rows);
        wc::print_top(rows, opts.top);
        if (!opts.stats.empty()) {
            wc::write_stats(opts.stats, summary);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
