#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t digest;
    uint64_t rows;
} FileSummary;

static int read_line(FILE *file, char **line, size_t *capacity, size_t *length) {
    int c;
    *length = 0;
    while ((c = fgetc(file)) != EOF) {
        if (*length + 2 > *capacity) {
            size_t next = *capacity ? *capacity * 2 : 256;
            char *grown = (char *)realloc(*line, next);
            if (!grown) {
                fprintf(stderr, "error: out of memory reading count table\n");
                exit(2);
            }
            *line = grown;
            *capacity = next;
        }
        if (c == '\n') break;
        (*line)[(*length)++] = (char)c;
    }
    if (c == EOF && *length == 0) return 0;
    (*line)[*length] = '\0';
    return 1;
}

static uint64_t digest_line(uint64_t digest, const char *line, size_t length) {
    size_t i;
    for (i = 0; i < length; ++i) {
        digest ^= (unsigned char)line[i];
        digest *= UINT64_C(1099511628211);
    }
    digest ^= (unsigned char)'\n';
    digest *= UINT64_C(1099511628211);
    return digest;
}

static void usage(void) {
    fprintf(stderr, "usage: wc_verify <a.tsv> <b.tsv> [--max-diff N]\n");
}

int main(int argc, char **argv) {
    const char *paths[2];
    int path_count = 0;
    int max_diff = 10;
    int i, shown = 0, match = 1;
    uint64_t row_number = 0;
    FILE *a, *b;
    char *line_a = NULL, *line_b = NULL;
    size_t capacity_a = 0, capacity_b = 0, length_a, length_b;
    FileSummary summary_a = {UINT64_C(14695981039346656037), 0};
    FileSummary summary_b = {UINT64_C(14695981039346656037), 0};

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--max-diff") == 0) {
            char *end;
            long parsed;
            if (i + 1 >= argc) {
                fprintf(stderr, "error: missing value for --max-diff\n");
                usage();
                return 2;
            }
            parsed = strtol(argv[++i], &end, 10);
            if (*end || parsed < 0 || parsed > 1000000) {
                fprintf(stderr, "error: invalid --max-diff value\n");
                return 2;
            }
            max_diff = (int)parsed;
        } else if (path_count < 2) {
            paths[path_count++] = argv[i];
        } else {
            usage();
            return 2;
        }
    }
    if (path_count != 2) {
        usage();
        return 2;
    }
    a = fopen(paths[0], "rb");
    b = fopen(paths[1], "rb");
    if (!a || !b) {
        fprintf(stderr, "error: cannot open count tables\n");
        if (a) fclose(a);
        if (b) fclose(b);
        return 2;
    }

    while (1) {
        int has_a = read_line(a, &line_a, &capacity_a, &length_a);
        int has_b = read_line(b, &line_b, &capacity_b, &length_b);
        if (!has_a && !has_b) break;
        ++row_number;
        if (has_a) {
            summary_a.digest = digest_line(summary_a.digest, line_a, length_a);
            ++summary_a.rows;
        }
        if (has_b) {
            summary_b.digest = digest_line(summary_b.digest, line_b, length_b);
            ++summary_b.rows;
        }
        if (has_a != has_b || (has_a && (length_a != length_b ||
                                          memcmp(line_a, line_b, length_a) != 0))) {
            match = 0;
            if (shown < max_diff) {
                printf("diff at row %" PRIu64 ":\n  A: %s\n  B: %s\n",
                       row_number,
                       has_a ? line_a : "<EOF>", has_b ? line_b : "<EOF>");
                ++shown;
            }
        }
    }
    fclose(a);
    fclose(b);
    free(line_a);
    free(line_b);

    printf("A: %s (%" PRIu64 " rows, digest %" PRIx64 ")\n",
           paths[0], summary_a.rows, summary_a.digest);
    printf("B: %s (%" PRIu64 " rows, digest %" PRIx64 ")\n",
           paths[1], summary_b.rows, summary_b.digest);
    if (summary_a.rows != summary_b.rows) match = 0;
    printf("%s\n", match ? "MATCH" : "MISMATCH");
    return match ? 0 : 1;
}
