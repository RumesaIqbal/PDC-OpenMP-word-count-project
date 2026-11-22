#include "wc.h"

#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int threads;
    const char *schedule;
    const char *merge;
    size_t chunks;
} WcParallelOptions;

static void usage(void) {
    fprintf(stderr, "usage: wc_par <file> [--threads T] [--schedule static|dynamic] "
                    "[--num-chunks K] [--merge critical|tree] [--top N] "
                    "[--out FILE] [--stats FILE] [--buffer-bytes N]\n");
}

static int parse_parallel(char **extra, size_t count, WcParallelOptions *options,
                          char *error, size_t error_capacity) {
    size_t i;
    options->threads = omp_get_max_threads();
    options->schedule = "static";
    options->merge = "critical";
    options->chunks = 0;
    for (i = 0; i < count; ++i) {
        const char *flag = extra[i];
        const char *value;
        uint64_t number;
        if (strcmp(flag, "--threads") != 0 && strcmp(flag, "--schedule") != 0 &&
            strcmp(flag, "--num-chunks") != 0 && strcmp(flag, "--merge") != 0) {
            snprintf(error, error_capacity, "unknown flag: %s", flag);
            return 0;
        }
        if (++i >= count) {
            snprintf(error, error_capacity, "missing value for %s", flag);
            return 0;
        }
        value = extra[i];
        if (strcmp(flag, "--threads") == 0) {
            if (!wc_parse_u64(value, 1, 256, &number)) {
                snprintf(error, error_capacity, "invalid --threads value: %s", value);
                return 0;
            }
            options->threads = (int)number;
        } else if (strcmp(flag, "--num-chunks") == 0) {
            if (!wc_parse_u64(value, 1, 1000000, &number)) {
                snprintf(error, error_capacity, "invalid --num-chunks value: %s", value);
                return 0;
            }
            options->chunks = (size_t)number;
        } else if (strcmp(flag, "--schedule") == 0) {
            if (strcmp(value, "static") != 0 && strcmp(value, "dynamic") != 0) {
                snprintf(error, error_capacity, "--schedule must be static or dynamic");
                return 0;
            }
            options->schedule = value;
        } else {
            if (strcmp(value, "critical") != 0 && strcmp(value, "tree") != 0) {
                snprintf(error, error_capacity, "--merge must be critical or tree");
                return 0;
            }
            options->merge = value;
        }
    }
    return 1;
}

static void merge_tree(WcMap *maps, int map_count, int threads) {
    int count = map_count;
    while (count > 1) {
        int pairs = (count + 1) / 2;
        int i;
        #pragma omp parallel for schedule(static) num_threads(threads)
        for (i = 0; i < pairs; ++i) {
            int a = 2 * i;
            int b = a + 1;
            if (b < count) {
                if (maps[b].used > maps[a].used) {
                    WcMap swap = maps[a];
                    maps[a] = maps[b];
                    maps[b] = swap;
                }
                wc_map_merge(&maps[a], &maps[b]);
                wc_map_destroy(&maps[b]);
            }
        }
        for (i = 0; i < pairs; ++i) {
            int source = 2 * i;
            if (i != source) {
                maps[i] = maps[source];
                maps[source].entries = NULL;
                maps[source].capacity = 0;
                maps[source].used = 0;
            }
        }
        count = pairs;
    }
}

int main(int argc, char **argv) {
    WcCommonOptions common;
    WcParallelOptions parallel;
    WcSummary summary = {0};
    WcMap *locals;
    WcMap global;
    WcTokenizer *tokenizers;
    uint64_t *bounds;
    double *read_times, *count_times, *wait_times, *hold_times;
    uint64_t *token_counts;
    char *extra[256];
    size_t extra_count = 0;
    char error[512];
    int chunk_count, actual_threads = 1, tid;
    size_t requested_chunks;
    double started;
    WcEntry **rows;

    if (!wc_parse_common(argc, argv, &common, extra, 256, &extra_count,
                         error, sizeof(error)) ||
        !parse_parallel(extra, extra_count, &parallel, error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        usage();
        return 2;
    }

    requested_chunks = parallel.chunks ? parallel.chunks :
        (parallel.schedule[0] == 'd' ? (size_t)8 * (size_t)parallel.threads
                                      : (size_t)parallel.threads);
    summary.impl = "parallel";
    summary.schedule = parallel.schedule;
    summary.merge_mode = parallel.merge;
    summary.threads = parallel.threads;
    summary.num_chunks = requested_chunks;
    summary.buffer_bytes = common.buffer_bytes;
    summary.size_bytes = wc_file_size(common.input);

    started = wc_now();
    {
        double setup_started = wc_now();
        bounds = wc_compute_boundaries(common.input, summary.size_bytes,
                                       requested_chunks, &chunk_count);
        summary.t_setup = wc_now() - setup_started;
    }
    summary.num_chunks = (size_t)chunk_count;

    locals = (WcMap *)calloc((size_t)parallel.threads, sizeof(WcMap));
    tokenizers = (WcTokenizer *)calloc((size_t)parallel.threads, sizeof(WcTokenizer));
    read_times = (double *)calloc((size_t)parallel.threads, sizeof(double));
    count_times = (double *)calloc((size_t)parallel.threads, sizeof(double));
    wait_times = (double *)calloc((size_t)parallel.threads, sizeof(double));
    hold_times = (double *)calloc((size_t)parallel.threads, sizeof(double));
    token_counts = (uint64_t *)calloc((size_t)parallel.threads, sizeof(uint64_t));
    if (!locals || !tokenizers || !read_times || !count_times || !wait_times ||
        !hold_times || !token_counts) wc_die("out of memory allocating thread state");
    for (tid = 0; tid < parallel.threads; ++tid) {
        wc_map_init(&locals[tid]);
        wc_tokenizer_init(&tokenizers[tid], &locals[tid]);
    }
    wc_map_init(&global);

    {
        double parallel_started = wc_now();
        #pragma omp parallel num_threads(parallel.threads) shared(actual_threads)
        {
            int thread_id = omp_get_thread_num();
            WcTokenizer *tokenizer = &tokenizers[thread_id];
            #pragma omp single
            actual_threads = omp_get_num_threads();

            if (strcmp(parallel.schedule, "dynamic") == 0) {
                int chunk;
                #pragma omp for schedule(dynamic)
                for (chunk = 0; chunk < chunk_count; ++chunk) {
                    wc_count_range(common.input, bounds[chunk], bounds[chunk + 1],
                                   common.buffer_bytes, tokenizer,
                                   &read_times[thread_id], &count_times[thread_id]);
                }
            } else {
                int chunk;
                #pragma omp for schedule(static)
                for (chunk = 0; chunk < chunk_count; ++chunk) {
                    wc_count_range(common.input, bounds[chunk], bounds[chunk + 1],
                                   common.buffer_bytes, tokenizer,
                                   &read_times[thread_id], &count_times[thread_id]);
                }
            }

            wc_tokenizer_finish(tokenizer);
            token_counts[thread_id] = tokenizer->tokens;
            if (strcmp(parallel.merge, "critical") == 0) {
                double wait_started = wc_now();
                double entered, left;
                #pragma omp critical(wc_merge)
                {
                    entered = wc_now();
                    if (actual_threads == 1) {
                        wc_map_destroy(&global);
                        global = locals[thread_id];
                        locals[thread_id].entries = NULL;
                        locals[thread_id].capacity = 0;
                        locals[thread_id].used = 0;
                    } else {
                        wc_map_merge(&global, &locals[thread_id]);
                    }
                    left = wc_now();
                }
                wait_times[thread_id] = entered - wait_started;
                hold_times[thread_id] = left - entered;
            }
        }
        summary.t_parallel = wc_now() - parallel_started;
    }

    if (strcmp(parallel.merge, "tree") == 0) {
        double merge_started = wc_now();
        merge_tree(locals, actual_threads, parallel.threads);
        global = locals[0];
        locals[0].entries = NULL;
        locals[0].capacity = 0;
        locals[0].used = 0;
        summary.t_merge_hold = wc_now() - merge_started;
        summary.t_parallel += summary.t_merge_hold;
    }
    summary.threads = actual_threads;
    for (tid = 0; tid < actual_threads; ++tid) {
        summary.total_tokens += token_counts[tid];
        summary.t_read += read_times[tid];
        summary.t_count += count_times[tid];
        summary.t_merge_wait += wait_times[tid];
        summary.t_merge_hold += hold_times[tid];
    }
    summary.unique_words = global.used;
    {
        double topn_started = wc_now();
        rows = wc_sorted_entries(&global);
        summary.t_topn = wc_now() - topn_started;
    }
    summary.t_total = wc_now() - started;
    summary.peak_rss_kb = wc_peak_rss_kb();

    wc_write_counts(common.out, rows, global.used, common.top);
    free(rows);
    if (common.stats[0]) wc_write_stats(common.stats, &summary);

    for (tid = 0; tid < parallel.threads; ++tid) {
        wc_tokenizer_destroy(&tokenizers[tid]);
        wc_map_destroy(&locals[tid]);
    }
    wc_map_destroy(&global);
    free(bounds);
    free(locals);
    free(tokenizers);
    free(read_times);
    free(count_times);
    free(wait_times);
    free(hold_times);
    free(token_counts);
    return 0;
}
