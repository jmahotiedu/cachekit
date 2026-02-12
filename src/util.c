#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

static ck_log_level_t g_log_level = CK_LOG_INFO;
static size_t g_mem_used = 0;

void *ck_malloc(size_t size) {
    void *p = malloc(size);
    if (!p && size > 0) {
        fprintf(stderr, "fatal: out of memory allocating %zu bytes\n", size);
        abort();
    }
    return p;
}

void *ck_calloc(size_t nmemb, size_t size) {
    void *p = calloc(nmemb, size);
    if (!p && nmemb > 0 && size > 0) {
        fprintf(stderr, "fatal: out of memory allocating %zu bytes\n", nmemb * size);
        abort();
    }
    return p;
}

void *ck_realloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (!p && size > 0) {
        fprintf(stderr, "fatal: out of memory reallocating %zu bytes\n", size);
        abort();
    }
    return p;
}

char *ck_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *dup = ck_malloc(len + 1);
    memcpy(dup, s, len + 1);
    return dup;
}

char *ck_strndup(const char *s, size_t n) {
    if (!s) return NULL;
    const char *end = memchr(s, '\0', n);
    size_t len = end ? (size_t)(end - s) : n;
    char *dup = ck_malloc(len + 1);
    memcpy(dup, s, len);
    dup[len] = '\0';
    return dup;
}

void ck_log_set_level(ck_log_level_t level) {
    g_log_level = level;
}

void ck_log(ck_log_level_t level, const char *fmt, ...) {
    if (level < g_log_level) return;

    const char *prefix;
    switch (level) {
        case CK_LOG_DEBUG: prefix = "DEBUG"; break;
        case CK_LOG_INFO:  prefix = "INFO";  break;
        case CK_LOG_WARN:  prefix = "WARN";  break;
        case CK_LOG_ERROR: prefix = "ERROR"; break;
        default:           prefix = "???";   break;
    }

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm);

    fprintf(stderr, "[%s] %s: ", timebuf, prefix);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
}

int64_t ck_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* simple glob matching supporting * and ? */
int ck_glob_match(const char *pattern, const char *string) {
    while (*pattern && *string) {
        if (*pattern == '*') {
            /* skip consecutive stars */
            while (*pattern == '*') pattern++;
            if (*pattern == '\0') return 1;

            /* try matching rest of pattern at every position */
            while (*string) {
                if (ck_glob_match(pattern, string)) return 1;
                string++;
            }
            return ck_glob_match(pattern, string);
        } else if (*pattern == '?' || *pattern == *string) {
            pattern++;
            string++;
        } else {
            return 0;
        }
    }

    /* trailing stars match empty */
    while (*pattern == '*') pattern++;

    return *pattern == '\0' && *string == '\0';
}

int ck_str_to_int64(const char *s, int64_t *out) {
    if (!s || !*s) return -1;

    char *end;
    errno = 0;
    long long val = strtoll(s, &end, 10);
    if (errno != 0 || *end != '\0') return -1;

    *out = (int64_t)val;
    return 0;
}

void ck_mem_track_alloc(size_t bytes) {
    g_mem_used += bytes;
}

void ck_mem_track_free(size_t bytes) {
    if (bytes > g_mem_used) {
        g_mem_used = 0;
    } else {
        g_mem_used -= bytes;
    }
}

size_t ck_mem_used(void) {
    return g_mem_used;
}
