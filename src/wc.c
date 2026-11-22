#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L

#include "wc.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <io.h>
#define wc_fseek _fseeki64
#define wc_stat _stat64
#else
#include <unistd.h>
#define wc_fseek fseeko
#define wc_stat stat
#endif

#include <omp.h>

#define WC_INITIAL_CAPACITY 1024u
#define WC_LOAD_NUMERATOR 7u
#define WC_LOAD_DENOMINATOR 10u

double wc_now(void) {
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&counter))
        return (double)GetTickCount64() / 1000.0;
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0.0;
    return (double)value.tv_sec + (double)value.tv_nsec / 1000000000.0;
#endif
}

void wc_die(const char *message) {
    fprintf(stderr, "error: %s\n", message);
    exit(1);
}

static void *wc_malloc(size_t size) {
    void *result = malloc(size ? size : 1);
    if (!result) wc_die("out of memory");
    return result;
}

static void *wc_calloc(size_t count, size_t size) {
    void *result = calloc(count ? count : 1, size ? size : 1);
    if (!result) wc_die("out of memory");
    return result;
}

static void *wc_realloc(void *ptr, size_t size) {
    void *result = realloc(ptr, size ? size : 1);
    if (!result) wc_die("out of memory");
    return result;
}

int wc_parse_u64(const char *text, uint64_t min_value, uint64_t max_value,
                 uint64_t *value) {
    char *end = NULL;
    unsigned long long parsed;
    if (!text || !*text || *text == '-') return 0;
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno || !end || *end || parsed < min_value || parsed > max_value) return 0;
    *value = (uint64_t)parsed;
    return 1;
}

static int common_flag(const char *flag) {
    return strcmp(flag, "--top") == 0 || strcmp(flag, "--out") == 0 ||
           strcmp(flag, "--stats") == 0 || strcmp(flag, "--buffer-bytes") == 0;
}

int wc_parse_common(int argc, char **argv, WcCommonOptions *options,
                    char **extra, size_t extra_capacity, size_t *extra_count,
                    char *error, size_t error_capacity) {
    int have_input = 0;
    size_t i;
    options->input = NULL;
    options->out = "";
    options->stats = "";
    options->top = 20;
    options->buffer_bytes = 4u * 1024u * 1024u;
    *extra_count = 0;
    error[0] = '\0';

    for (i = 1; i < (size_t)argc; ++i) {
        const char *arg = argv[i];
        if (common_flag(arg)) {
            const char *value;
            uint64_t number;
            if (i + 1 >= (size_t)argc) {
                snprintf(error, error_capacity, "missing value for %s", arg);
                return 0;
            }
            value = argv[++i];
            if (strcmp(arg, "--top") == 0) {
                if (!wc_parse_u64(value, 1, 1000000, &number)) {
                    snprintf(error, error_capacity, "invalid --top value: %s", value);
                    return 0;
                }
                options->top = (size_t)number;
            } else if (strcmp(arg, "--buffer-bytes") == 0) {
                if (!wc_parse_u64(value, 1, UINT64_C(1) << 34, &number)) {
                    snprintf(error, error_capacity, "invalid --buffer-bytes value: %s", value);
                    return 0;
                }
                options->buffer_bytes = (size_t)number;
            } else if (strcmp(arg, "--out") == 0) {
                options->out = value;
            } else {
                options->stats = value;
            }
        } else if (arg[0] == '-' && arg[1] == '-') {
            if (*extra_count >= extra_capacity) {
                snprintf(error, error_capacity, "too many command-line arguments");
                return 0;
            }
            extra[(*extra_count)++] = argv[i];
            if (i + 1 < (size_t)argc && argv[i + 1][0] != '-') {
                if (*extra_count >= extra_capacity) {
                    snprintf(error, error_capacity, "too many command-line arguments");
                    return 0;
                }
                extra[(*extra_count)++] = argv[++i];
            }
        } else if (!have_input) {
            options->input = arg;
            have_input = 1;
        } else {
            snprintf(error, error_capacity, "unexpected positional argument: %s", arg);
            return 0;
        }
    }
    if (!have_input) {
        snprintf(error, error_capacity, "missing input file");
        return 0;
    }
    return 1;
}

uint64_t wc_file_size(const char *path) {
    struct wc_stat info;
    if (wc_stat(path, &info) != 0 || info.st_size < 0) {
        char message[1024];
        snprintf(message, sizeof(message), "cannot stat '%s': %s", path, strerror(errno));
        wc_die(message);
    }
    return (uint64_t)info.st_size;
}

static unsigned char lower_ascii(unsigned char c) {
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c - 'A' + 'a') : c;
}

static int is_delimiter(unsigned char c) {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '\'');
}

static uint64_t hash_word(const char *word, size_t length, int already_lowercase) {
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i;
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)word[i];
        if (!already_lowercase) c = lower_ascii(c);
        hash ^= c;
        hash *= UINT64_C(1099511628211);
    }
    /* Mix high bits into the low bits used by the power-of-two table mask. */
    hash ^= hash >> 33;
    hash *= UINT64_C(0xff51afd7ed558ccd);
    hash ^= hash >> 33;
    hash *= UINT64_C(0xc4ceb9fe1a85ec53);
    hash ^= hash >> 33;
    return hash;
}

static int key_matches(const char *key, const char *word, size_t length,
                       int already_lowercase) {
    size_t i;
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)word[i];
        if (!already_lowercase) c = lower_ascii(c);
        if ((unsigned char)key[i] != c) return 0;
    }
    return key[length] == '\0';
}

void wc_map_init(WcMap *map) {
    map->capacity = WC_INITIAL_CAPACITY;
    map->used = 0;
    map->entries = (WcEntry *)wc_calloc(map->capacity, sizeof(WcEntry));
}

void wc_map_destroy(WcMap *map) {
    size_t i;
    for (i = 0; i < map->capacity; ++i) free(map->entries[i].key);
    free(map->entries);
    map->entries = NULL;
    map->capacity = 0;
    map->used = 0;
}

static void map_rehash(WcMap *map, size_t new_capacity) {
    WcEntry *old_entries = map->entries;
    size_t old_capacity = map->capacity;
    size_t i;
    map->entries = (WcEntry *)wc_calloc(new_capacity, sizeof(WcEntry));
    map->capacity = new_capacity;
    map->used = 0;
    for (i = 0; i < old_capacity; ++i) {
        if (old_entries[i].key) {
            size_t slot = (size_t)hash_word(old_entries[i].key,
                                            strlen(old_entries[i].key), 1) &
                          (new_capacity - 1);
            while (map->entries[slot].key) slot = (slot + 1) & (new_capacity - 1);
            map->entries[slot] = old_entries[i];
            ++map->used;
        }
    }
    free(old_entries);
}

static void map_reserve_entries(WcMap *map, size_t desired) {
    size_t capacity = map->capacity;
    while (desired > (capacity * WC_LOAD_NUMERATOR) / WC_LOAD_DENOMINATOR) {
        if (capacity > SIZE_MAX / 2) wc_die("word table capacity overflow");
        capacity *= 2;
    }
    if (capacity != map->capacity) map_rehash(map, capacity);
}

void wc_map_add_span(WcMap *map, const char *word, size_t length,
                     int already_lowercase) {
    uint64_t hash;
    size_t slot;
    size_t i;
    char *copy;
    if (!length) return;
    hash = hash_word(word, length, already_lowercase);
    slot = (size_t)hash & (map->capacity - 1);
    while (map->entries[slot].key) {
        if (key_matches(map->entries[slot].key, word, length, already_lowercase)) {
            ++map->entries[slot].count;
            return;
        }
        slot = (slot + 1) & (map->capacity - 1);
    }
    if ((map->used + 1) * WC_LOAD_DENOMINATOR >=
        map->capacity * WC_LOAD_NUMERATOR) {
        if (map->capacity > SIZE_MAX / 2) wc_die("word table capacity overflow");
        map_rehash(map, map->capacity * 2);
        slot = (size_t)hash & (map->capacity - 1);
        while (map->entries[slot].key) slot = (slot + 1) & (map->capacity - 1);
    }
    copy = (char *)wc_malloc(length + 1);
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)word[i];
        copy[i] = (char)(already_lowercase ? c : lower_ascii(c));
    }
    copy[length] = '\0';
    map->entries[slot].key = copy;
    map->entries[slot].count = 1;
    ++map->used;
}

void wc_map_merge(WcMap *destination, const WcMap *source) {
    size_t i;
    if (source->used > SIZE_MAX - destination->used)
        wc_die("word table capacity overflow during merge");
    map_reserve_entries(destination, destination->used + source->used);
    for (i = 0; i < source->capacity; ++i) {
        if (source->entries[i].key) {
            uint64_t amount = source->entries[i].count;
            const char *key = source->entries[i].key;
            size_t length = strlen(key);
            /* Add once to locate/allocate the key, then add the remaining count. */
            wc_map_add_span(destination, key, length, 1);
            {
                size_t slot = (size_t)hash_word(key, length, 1) &
                              (destination->capacity - 1);
                while (strcmp(destination->entries[slot].key, key) != 0)
                    slot = (slot + 1) & (destination->capacity - 1);
                destination->entries[slot].count += amount - 1;
            }
        }
    }
}

void wc_tokenizer_init(WcTokenizer *tokenizer, WcMap *map) {
    tokenizer->map = map;
    tokenizer->carry = NULL;
    tokenizer->carry_len = 0;
    tokenizer->carry_capacity = 0;
    tokenizer->tokens = 0;
}

static void tokenizer_append(WcTokenizer *tokenizer, const char *data, size_t length) {
    size_t i;
    size_t needed = tokenizer->carry_len + length + 1;
    if (needed > tokenizer->carry_capacity) {
        size_t capacity = tokenizer->carry_capacity ? tokenizer->carry_capacity : 64;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) wc_die("token is too long");
            capacity *= 2;
        }
        tokenizer->carry = (char *)wc_realloc(tokenizer->carry, capacity);
        tokenizer->carry_capacity = capacity;
    }
    for (i = 0; i < length; ++i)
        tokenizer->carry[tokenizer->carry_len + i] =
            (char)lower_ascii((unsigned char)data[i]);
    tokenizer->carry_len += length;
    tokenizer->carry[tokenizer->carry_len] = '\0';
}

static void tokenizer_flush(WcTokenizer *tokenizer) {
    if (tokenizer->carry_len) {
        wc_map_add_span(tokenizer->map, tokenizer->carry, tokenizer->carry_len, 1);
        ++tokenizer->tokens;
        tokenizer->carry_len = 0;
        if (tokenizer->carry) tokenizer->carry[0] = '\0';
    }
}

void wc_tokenizer_feed(WcTokenizer *tokenizer, const char *data, size_t length) {
    size_t i = 0;
    while (i < length) {
        size_t j;
        if (is_delimiter((unsigned char)data[i])) {
            tokenizer_flush(tokenizer);
            ++i;
            continue;
        }
        j = i + 1;
        while (j < length && !is_delimiter((unsigned char)data[j])) ++j;
        if (!tokenizer->carry_len && j < length) {
            wc_map_add_span(tokenizer->map, data + i, j - i, 0);
            ++tokenizer->tokens;
        } else {
            tokenizer_append(tokenizer, data + i, j - i);
            if (j < length) tokenizer_flush(tokenizer);
        }
        i = j;
    }
}

void wc_tokenizer_finish(WcTokenizer *tokenizer) {
    tokenizer_flush(tokenizer);
}

void wc_tokenizer_destroy(WcTokenizer *tokenizer) {
    free(tokenizer->carry);
    tokenizer->carry = NULL;
    tokenizer->carry_len = 0;
    tokenizer->carry_capacity = 0;
}

static int seek_file(FILE *file, uint64_t offset) {
#ifdef _WIN32
    return wc_fseek(file, (long long)offset, SEEK_SET);
#else
    return wc_fseek(file, (off_t)offset, SEEK_SET);
#endif
}

static uint64_t snap_forward(const char *path, uint64_t offset, uint64_t size) {
    FILE *file;
    uint64_t position = offset;
    int c;
    if (offset == 0 || offset >= size) return offset >= size ? size : 0;
    file = fopen(path, "rb");
    if (!file) {
        char message[1024];
        snprintf(message, sizeof(message), "cannot open '%s' for boundary scan", path);
        wc_die(message);
    }
    if (seek_file(file, offset) != 0) wc_die("cannot seek while computing chunk boundaries");
    while ((c = fgetc(file)) != EOF) {
        if (is_delimiter((unsigned char)c)) {
            fclose(file);
            return position;
        }
        ++position;
    }
    fclose(file);
    return size;
}

uint64_t *wc_compute_boundaries(const char *path, uint64_t size,
                                size_t requested_chunks, int *chunk_count) {
    uint64_t *bounds;
    size_t used = 1;
    size_t i;
    if (!requested_chunks) requested_chunks = 1;
    bounds = (uint64_t *)wc_malloc((requested_chunks + 1) * sizeof(uint64_t));
    bounds[0] = 0;
    for (i = 1; i < requested_chunks; ++i) {
        uint64_t raw = (size / requested_chunks) * i +
                       ((size % requested_chunks) * i) / requested_chunks;
        uint64_t snapped = snap_forward(path, raw, size);
        if (snapped > bounds[used - 1] && snapped < size) bounds[used++] = snapped;
    }
    if (bounds[used - 1] < size) bounds[used++] = size;
    *chunk_count = (int)(used - 1);
    return bounds;
}

void wc_count_range(const char *path, uint64_t begin, uint64_t end,
                    size_t buffer_bytes, WcTokenizer *tokenizer,
                    double *read_seconds, double *count_seconds) {
    FILE *file = fopen(path, "rb");
    char *buffer;
    uint64_t position = begin;
    if (!file) {
        char message[1024];
        snprintf(message, sizeof(message), "cannot open '%s' for reading", path);
        wc_die(message);
    }
    if (seek_file(file, begin) != 0) wc_die("cannot seek to chunk start");
    buffer = (char *)wc_malloc(buffer_bytes);
    while (position < end) {
        size_t wanted = (size_t)((end - position) < buffer_bytes ? end - position : buffer_bytes);
        double started = wc_now();
        size_t got = fread(buffer, 1, wanted, file);
        *read_seconds += wc_now() - started;
        if (!got) {
            if (ferror(file)) wc_die("failed reading input file");
            break;
        }
        position += (uint64_t)got;
        started = wc_now();
        wc_tokenizer_feed(tokenizer, buffer, got);
        *count_seconds += wc_now() - started;
    }
    free(buffer);
    fclose(file);
}

static int compare_entries(const void *left, const void *right) {
    const WcEntry *a = *(const WcEntry *const *)left;
    const WcEntry *b = *(const WcEntry *const *)right;
    if (a->count > b->count) return -1;
    if (a->count < b->count) return 1;
    return strcmp(a->key, b->key);
}

WcEntry **wc_sorted_entries(const WcMap *map) {
    WcEntry **rows;
    size_t i, row = 0;
    rows = (WcEntry **)wc_malloc((map->used ? map->used : 1) * sizeof(WcEntry *));
    for (i = 0; i < map->capacity; ++i)
        if (map->entries[i].key) rows[row++] = (WcEntry *)&map->entries[i];
    qsort(rows, row, sizeof(WcEntry *), compare_entries);
    return rows;
}

void wc_write_counts(const char *path, WcEntry **rows, size_t row_count, size_t top) {
    size_t i;
    FILE *output = NULL;
    if (path && *path) {
        output = fopen(path, "wb");
        if (!output) {
            char message[1024];
            snprintf(message, sizeof(message), "cannot write counts file '%s'", path);
            wc_die(message);
        }
    }
    for (i = 0; i < row_count; ++i) {
        if (output && fprintf(output, "%s\t%" PRIu64 "\n", rows[i]->key, rows[i]->count) < 0)
            wc_die("failed writing counts file");
        if (i < top) printf("%s\t%" PRIu64 "\n", rows[i]->key, rows[i]->count);
    }
    if (output && fclose(output) != 0) wc_die("failed closing counts file");
}

uint64_t wc_peak_rss_kb(void) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return (uint64_t)(counters.PeakWorkingSetSize / 1024u);
    return 0;
#else
    FILE *status = fopen("/proc/self/status", "r");
    char line[256];
    uint64_t result = 0;
    if (!status) return 0;
    while (fgets(line, sizeof(line), status)) {
        if (strncmp(line, "VmHWM:", 6) == 0) {
            unsigned long long value = 0;
            if (sscanf(line + 6, "%llu", &value) == 1) result = (uint64_t)value;
            break;
        }
    }
    fclose(status);
    return result;
#endif
}

static void write_json_string(FILE *file, const char *text) {
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    fputc('"', file);
    while (*p) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', file);
            fputc(*p, file);
        } else if (*p == '\n') fputs("\\n", file);
        else if (*p == '\r') fputs("\\r", file);
        else if (*p == '\t') fputs("\\t", file);
        else fputc(*p, file);
        ++p;
    }
    fputc('"', file);
}

void wc_write_stats(const char *path, const WcSummary *s) {
    FILE *out = fopen(path, "wb");
    if (!out) {
        char message[1024];
        snprintf(message, sizeof(message), "cannot write stats file '%s'", path);
        wc_die(message);
    }
    fputs("{\n  \"impl\": ", out); write_json_string(out, s->impl);
    fputs(",\n  \"schedule\": ", out); write_json_string(out, s->schedule);
    fputs(",\n  \"merge\": ", out); write_json_string(out, s->merge_mode);
    fprintf(out, ",\n  \"threads\": %d,\n  \"num_chunks\": %zu,\n"
                 "  \"buffer_bytes\": %zu,\n  \"size_bytes\": %" PRIu64 ",\n"
                 "  \"total_tokens\": %" PRIu64 ",\n  \"unique_words\": %zu,\n"
                 "  \"peak_rss_kb\": %" PRIu64 ",\n  \"t_setup\": %.9f,\n"
                 "  \"t_parallel\": %.9f,\n  \"t_read\": %.9f,\n"
                 "  \"t_count\": %.9f,\n  \"t_merge_wait\": %.9f,\n"
                 "  \"t_merge_hold\": %.9f,\n  \"t_topn\": %.9f,\n"
                 "  \"t_total\": %.9f\n}\n",
            s->threads, s->num_chunks, s->buffer_bytes, s->size_bytes,
            s->total_tokens, s->unique_words, s->peak_rss_kb, s->t_setup,
            s->t_parallel, s->t_read, s->t_count, s->t_merge_wait,
            s->t_merge_hold, s->t_topn, s->t_total);
    if (fclose(out) != 0) wc_die("failed closing stats file");
}

