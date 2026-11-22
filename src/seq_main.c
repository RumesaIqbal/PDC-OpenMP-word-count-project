#include "wc.h"

#include <stdio.h>
#include <stdlib.h>

static void usage(void) {
    fprintf(stderr, "usage: wc_seq <file> [--top N] [--out FILE] [--stats FILE] "
                    "[--buffer-bytes N]\n");
}

int main(int argc, char **argv) {
    WcCommonOptions options;
    WcSummary summary = {0};
    WcMap counter;
    WcTokenizer tokenizer;
    WcEntry **rows;
    char *extra[256];
    size_t extra_count = 0;
    char error[512];
    double started;

    if (!wc_parse_common(argc, argv, &options, extra, 256, &extra_count,
                         error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        usage();
        return 2;
    }
    if (extra_count) {
        fprintf(stderr, "error: unknown flag: %s\n", extra[0]);
        usage();
        return 2;
    }

    summary.impl = "sequential";
    summary.schedule = "none";
    summary.merge_mode = "none";
    summary.threads = 1;
    summary.num_chunks = 1;
    summary.buffer_bytes = options.buffer_bytes;
    summary.size_bytes = wc_file_size(options.input);

    started = wc_now();
    wc_map_init(&counter);
    wc_tokenizer_init(&tokenizer, &counter);
    wc_count_range(options.input, 0, summary.size_bytes, options.buffer_bytes,
                   &tokenizer, &summary.t_read, &summary.t_count);
    wc_tokenizer_finish(&tokenizer);
    summary.total_tokens = tokenizer.tokens;
    wc_tokenizer_destroy(&tokenizer);

    {
        double topn_started = wc_now();
        rows = wc_sorted_entries(&counter);
        summary.t_topn = wc_now() - topn_started;
    }
    summary.t_total = wc_now() - started;
    summary.unique_words = counter.used;
    summary.peak_rss_kb = wc_peak_rss_kb();

    wc_write_counts(options.out, rows, counter.used, options.top);
    free(rows);
    if (options.stats[0]) wc_write_stats(options.stats, &summary);
    wc_map_destroy(&counter);
    return 0;
}
