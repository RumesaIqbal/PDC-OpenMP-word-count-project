#ifndef WC_H
#define WC_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *key;
    uint64_t count;
} WcEntry;

typedef struct {
    WcEntry *entries;
    size_t capacity;
    size_t used;
} WcMap;

typedef struct {
    WcMap *map;
    char *carry;
    size_t carry_len;
    size_t carry_capacity;
    uint64_t tokens;
} WcTokenizer;

typedef struct {
    const char *input;
    const char *out;
    const char *stats;
    size_t top;
    size_t buffer_bytes;
} WcCommonOptions;

typedef struct {
    const char *impl;
    const char *schedule;
    const char *merge_mode;
    int threads;
    size_t num_chunks;
    size_t buffer_bytes;
    uint64_t size_bytes;
    uint64_t total_tokens;
    size_t unique_words;
    uint64_t peak_rss_kb;
    double t_setup;
    double t_parallel;
    double t_read;
    double t_count;
    double t_merge_wait;
    double t_merge_hold;
    double t_topn;
    double t_total;
} WcSummary;

double wc_now(void);
void wc_die(const char *message);
int wc_parse_u64(const char *text, uint64_t min_value, uint64_t max_value,
                 uint64_t *value);
int wc_parse_common(int argc, char **argv, WcCommonOptions *options,
                    char **extra, size_t extra_capacity, size_t *extra_count,
                    char *error, size_t error_capacity);
uint64_t wc_file_size(const char *path);

void wc_map_init(WcMap *map);
void wc_map_destroy(WcMap *map);
void wc_map_add_span(WcMap *map, const char *word, size_t length,
                     int already_lowercase);
void wc_map_merge(WcMap *destination, const WcMap *source);

void wc_tokenizer_init(WcTokenizer *tokenizer, WcMap *map);
void wc_tokenizer_feed(WcTokenizer *tokenizer, const char *data, size_t length);
void wc_tokenizer_finish(WcTokenizer *tokenizer);
void wc_tokenizer_destroy(WcTokenizer *tokenizer);

uint64_t *wc_compute_boundaries(const char *path, uint64_t size,
                                size_t requested_chunks, int *chunk_count);
void wc_count_range(const char *path, uint64_t begin, uint64_t end,
                    size_t buffer_bytes, WcTokenizer *tokenizer,
                    double *read_seconds, double *count_seconds);

WcEntry **wc_sorted_entries(const WcMap *map);
void wc_write_counts(const char *path, WcEntry **rows, size_t row_count, size_t top);
uint64_t wc_peak_rss_kb(void);
void wc_write_stats(const char *path, const WcSummary *summary);

#endif
