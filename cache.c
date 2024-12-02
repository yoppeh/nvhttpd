/**
 * @file cache.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief cache module implementation.
 * @version 0.1.0
 * @date 2024-11-30
 * @copyright Copyright (c) 2024
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "debug.h"

static const size_t initial_capacity = 32768;

typedef struct cache_s {
    size_t capacity;
    size_t mask;
    size_t count;
    cache_element_s *data;
} cache_s;

static cache_s cache = {
    .capacity = initial_capacity,
    .mask = initial_capacity - 1,
    .count = 0,
    .data = NULL
}; 

static inline const char *determine_mime(cache_element_s *e);
static inline size_t hash(const char *key);
static inline int init_element(cache_element_s *e);
static inline cache_element_s *insert(const char *path, size_t full_hash);
static inline int resize(void);

cache_element_s *cache_find(const char *path) {
    debug_enter();
    size_t full_hash = hash(path);
    size_t index = full_hash & cache.mask;
    size_t index_original = index;
    while (cache.data[index].path != NULL) {
        if (strcmp(cache.data[index].path, path) == 0) {
            return &cache.data[index];
        }
        index = (index + 1) & cache.mask;
        if (index == index_original) {
            break;
        }
    }
    cache_element_s e;
    e.path = path;
    if (init_element(&e) == 1) {
        debug_return NULL;
    }
    cache_element_s *p = insert(path, full_hash);
    p->mime = e.mime;
    p->data = e.data;
    p->len = e.len;
    debug_return p;
}

void cache_free(void) {
    debug_enter();
    if (cache.data == NULL) {
        debug_return;
    }
    for (size_t i = 0; i < cache.capacity; i++) {
        if (cache.data[i].path != NULL) {
            free((char *)cache.data[i].path);
        }
        if (cache.data[i].data != NULL) {
            free((char *)cache.data[i].data);
        }
    }
    free(cache.data);
    memset(&cache, 0, sizeof(cache_s));
    debug_return;
}

int cache_init(void) {
    debug_enter();
    cache.capacity = initial_capacity;
    cache.mask = initial_capacity - 1;
    cache.count = 0;
    cache.data = calloc(cache.capacity, sizeof(cache_element_s));
    if (cache.data == NULL) {
        debug_return 1;
    }
    debug_return 0;
}

static inline const char *determine_mime(cache_element_s *e) {
    debug_enter();
    const char *mime = "application/octet-stream";
    if (strstr(e->path, ".css") != NULL) {
        mime = "text/css";
    } else if (strstr(e->path, ".js") != NULL) {
        mime = "application/javascript";
    } else if (strstr(e->path, ".jpg") != NULL || strstr(e->path, ".jpeg") != NULL) {
        mime = "image/jpeg";
    } else if (strstr(e->path, ".ico") != NULL) {
        mime = "image/x-icon";
    } else if (strstr(e->path, ".svg") != NULL) {
        mime = "image/svg+xml";
    } else if (strstr(e->path, ".webmanifest") != NULL) {
        mime = "application/manifest+json";
    } else if (strstr(e->path, ".html") != NULL) {
        mime = "text/html; charset=UTF-8";
    }
    debug_return mime;
}

static inline size_t hash(const char *key) {
    debug_enter();
    size_t hash = 0;
    for (size_t i = 0; key[i] != '\0'; i++) {
        hash = ((hash << 5) - hash) + key[i];
    }
    debug_return hash;
}

static inline int init_element(cache_element_s *e) {
    debug_enter();
    int rc = 1;
    e->len = 0;
    e->data = NULL;
    e->mime = NULL;
    FILE *f = fopen(e->path, "r");
    if (f == NULL) {
        goto term;
    }
    if (fseek(f, 0, SEEK_END)) {
        goto term;
    }
    e->len = ftell(f);
    e->data = malloc(e->len);
    if (e->data == NULL) {
        goto term;
    }
    if (fseek(f, 0, SEEK_SET)) {
        goto term;
    }
    if (fread((char *)e->data, 1, e->len, f) != e->len) {
        goto term;
    }
    e->mime = determine_mime(e);
    rc = 0;
term:
    if (e->data != NULL && rc != 0) {
        free((char *)e->data);
        e->len = 0;
    }
    if (f != NULL) {
        fclose(f);
    }
    if (rc != 0) {
        e->mime = NULL;
    }
    debug_return rc;
}

static inline cache_element_s *insert(const char *path, size_t full_hash) {
    debug_enter();
    size_t index;
    while (1) {
        index = full_hash & cache.mask;
        if (cache.data[index].path == NULL) {
            cache.data[index].path = strdup(path);
            cache.data[index].hash = full_hash;
            cache.count++;
            debug_return &cache.data[index];
        }
        if (strcmp(cache.data[index].path, path) == 0) {
            debug_return &cache.data[index];
        }
        if (cache.count == cache.capacity) {
            if (resize() != 0) {
                debug_return NULL;
            }
            continue;
        }
        break;
    }
    while (cache.data[index].path != NULL && strcmp(cache.data[index].path, path) != 0) {
        index = (index + 1) & cache.mask;
    }
    if (cache.data[index].path == NULL) {
        cache.data[index].path = strdup(path);
        cache.data[index].hash = full_hash;
        cache.count++;
    }
    debug_return &cache.data[index];
 }

static inline int resize(void) {
    debug_enter();
    cache_s new_cache;
    new_cache.capacity = cache.capacity << 1;
    new_cache.mask = new_cache.capacity - 1;
    new_cache.count = 0;
    new_cache.data = calloc(new_cache.capacity, sizeof(cache_element_s));
    if (new_cache.data == NULL) {
        debug_return 1;
    }
    for (size_t i = 0; i < cache.capacity; i++) {
        if (cache.data[i].path != NULL) {
            cache_element_s *e = insert(cache.data[i].path, cache.data[i].hash);
            if (e == NULL) {
                free(cache.data);
                debug_return 1;
            }
            memcpy(e, &cache.data[i], sizeof(cache_element_s));
        }
    }
    free(cache.data);
    memcpy(&cache, &new_cache, sizeof(cache_s));
    debug_return 0;
}
