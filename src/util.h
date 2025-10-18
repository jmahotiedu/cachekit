#ifndef CK_UTIL_H
#define CK_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* memory wrappers that abort on failure */
void *ck_malloc(size_t size);
void *ck_calloc(size_t nmemb, size_t size);
void *ck_realloc(void *ptr, size_t size);
char *ck_strdup(const char *s);
char *ck_strndup(const char *s, size_t n);

/* logging */
typedef enum {
    CK_LOG_DEBUG,
    CK_LOG_INFO,
    CK_LOG_WARN,
    CK_LOG_ERROR
} ck_log_level_t;

void ck_log_set_level(ck_log_level_t level);
void ck_log(ck_log_level_t level, const char *fmt, ...);

/* time helpers */
int64_t ck_time_ms(void);

/* string helpers */
int ck_glob_match(const char *pattern, const char *string);
int ck_str_to_int64(const char *s, int64_t *out);

/* memory tracking */
void ck_mem_track_alloc(size_t bytes);
void ck_mem_track_free(size_t bytes);
size_t ck_mem_used(void);

#endif
