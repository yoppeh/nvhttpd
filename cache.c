/**
 * @file cache.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief cache module implementation.
 * @version 0.1.0
 * @date 2024-11-30
 * @copyright Copyright (c) 2024
 */

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cache.h"
#include "debug.h"

static const int max_cache_elements = 65534;

typedef struct cache_s {
    log_s *log;
    size_t capacity;
    size_t mask;
    size_t count;
    cache_element_s *data;
} cache_s;

static cache_s *cache = NULL;
static pthread_rwlock_t cache_rw_lock;

static const char *determine_mime(cache_element_s *e);
static void free_cache(cache_s *cache);
static inline size_t hash(const char *key);
static int init_element(cache_element_s *e);
static cache_element_s *insert(const char *path, size_t full_hash);
static int load_dir(cache_s *cache, cache_element_s **list, const char const *base_path, const char const *path);

cache_element_s *cache_find(const char const *path) {
    debug_enter();
    cache_element_s *p = NULL;
    cache_element_s *r = NULL;
    size_t full_hash = hash(path);
    log_debug(cache->log, "Looking up hash %04x for path %s", full_hash, path);
    pthread_rwlock_rdlock(&cache_rw_lock);
    if (cache == NULL) {
        goto term;
    }
    size_t index = full_hash & cache->mask;
    size_t index_original = index;
    while (cache->data[index].path != NULL) {
        if (strcmp(cache->data[index].path, path) == 0) {
            debug("cache hit for path %s\n", path);
            p = &cache->data[index];
            goto term;
        }
        index = (index + 1) & cache->mask;
        if (index == index_original) {
            break;
        }
    }
term:
    if (p == NULL) {
        log_debug(cache->log, "Hash entry %04x not found in cache", full_hash);
    } else {
        size_t p_len = strlen(p->path) + 1;
        r = malloc(sizeof(cache_element_s) + p->len + p_len);
        r->next = NULL;
        r->hash = p->hash;
        r->len = p->len;
        r->path = (char *)r + sizeof(cache_element_s);
        r->mime = p->mime;
        r->data = r->path + p_len;
        memcpy(r->path, p->path, p_len);
        memcpy(r->data, p->data, r->len);
        log_debug(cache->log, "Found hash entry %04x: %s", full_hash, p->path);
    }
    pthread_rwlock_unlock(&cache_rw_lock);
    debug_return r;
}

int cache_init(void) {
    debug_enter();
    pthread_rwlock_init(&cache_rw_lock, NULL);
    debug_return 0;
}

int cache_load(const char const *path, log_s *log) {
    debug_enter();
    log_info(log, "Loading cache from %s", path);
    int rc = 1;
    cache_s *new = malloc(sizeof(cache_s));
    cache_element_s *file_list = NULL;
    if (new == NULL) {
        goto term;
    }
    new->log = log;
    new->mask = 0;
    new->count = 0;
    new->data = NULL;
    if (load_dir(new, &file_list, path, path) != 0) {
        free(new);
        new = NULL;
        goto term;
    }
    if (new->count > max_cache_elements) {
        debug("maximum cache capacity %d exceeded: %d", max_cache_elements, new->count);
        log_error(log, "number of files %d exceeds maximum cache capacity %d", new->count, max_cache_elements);
        free(new);
        goto term;
    }
    debug("caching %d files\n", new->count);
    if (new->count == 0) {
        free(new);
        new = NULL;
        goto term;
    }
    uint16_t capacity = new->count - 1;
    capacity |= capacity >> 1;
    capacity |= capacity >> 2;
    capacity |= capacity >> 3;
    capacity |= capacity >> 4;
    capacity |= capacity >> 5;
    capacity |= capacity >> 6;
    capacity |= capacity >> 7;
    capacity |= capacity >> 8;
    capacity |= capacity >> 9;
    capacity |= capacity >> 10;
    capacity |= capacity >> 11;
    capacity |= capacity >> 12;
    capacity |= capacity >> 13;
    capacity |= capacity >> 14;
    capacity |= capacity >> 15;
    capacity |= capacity >> 16;
    new->mask = capacity;
    capacity++;
    new->capacity = capacity;
    debug("cache capacity = %d elements\n", capacity);
    if ((new->data = calloc(capacity, sizeof(cache_element_s))) == NULL) {
        free(new);
        new = NULL;
        goto term;
    }
    if (file_list != NULL) {
        while (file_list) {
            size_t index = file_list->hash & new->mask;
            if (new->data[index].path == NULL) {
                new->data[index].path = file_list->path;
                file_list->path = NULL;
                new->data[index].data = file_list->data;
                file_list->data = NULL;
                new->data[index].len = file_list->len;
                new->data[index].mime = file_list->mime;
                new->data[index].next = NULL;
                new->data[index].hash = file_list->hash;
                debug("inserting %s, hash = %04x\n", new->data[index].path, new->data[index].hash);
            } else {
                while (new->data[index].path != NULL && strcmp(new->data[index].path, file_list->path) != 0) {
                    index = (index + 1) & new->mask;
                }
                if (new->data[index].path == NULL) {
                    new->data[index].path = file_list->path;
                    file_list->path = NULL;
                    new->data[index].data = file_list->data;
                    file_list->data = NULL;
                    new->data[index].len = file_list->len;
                    new->data[index].mime = file_list->mime;
                    new->data[index].next = NULL;
                    new->data[index].hash = file_list->hash;
                    debug("inserting %s, hash = %04x\n", new->data[index].path, new->data[index].hash);
                }
            }
            cache_element_s *e = file_list->next;
            free(file_list);
            file_list = e;
        }
    }
    cache_s *old = cache;
    pthread_rwlock_wrlock(&cache_rw_lock);
    cache = new;
    free_cache(old);
    pthread_rwlock_unlock(&cache_rw_lock);
    rc = 0;
term:
    if (file_list != NULL) {
        cache_element_s *e = file_list->next;
        if (file_list->data) {
            free((void *)file_list->data);
        }
        if (file_list->path) {
            free((void *)file_list->path);
        }
        free(file_list);
        file_list = e;
    }
    debug_return rc;
}

static const char *determine_mime(cache_element_s *e) {
    debug_enter();
    const char *cp = strrchr(e->path, '.');
    if (cp == NULL || *(cp + 1) == '\0') {
        debug_return "application/octet-stream";
    }
    cp++;
    if (strcasecmp(cp, "css") == 0) {
        debug_return "text/css";
    } 
    if (strcasecmp(cp, "docx") == 0) {
        debug_return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    }
    if (strcasecmp(cp, "html") == 0) {
        debug_return "text/html; charset=UTF-8";
    }
    if (strcasecmp(cp, "ico") == 0) {
        debug_return "image/x-icon";
    }
    if (strcasecmp(cp, "jpg") == 0 || strcasecmp(cp, "jpeg") == 0) {
        debug_return "image/jpeg";
    }
    if (strcasecmp(cp, "js") == 0) {
        debug_return "application/javascript";
    }
    if (strcasecmp(cp, "md") == 0) {
        debug_return "text/markdown";
    }
    if (strcasecmp(cp, "png") == 0) {
        debug_return "image/png";
    }
    if (strcasecmp(cp, "svg") == 0) {
        debug_return "image/svg+xml";    
    }
    if (strcasecmp(cp, "webmanifest") == 0) {
        debug_return "application/manifest+json";
    }
    if (strcasecmp(cp, "xml") == 0) {
        debug_return "text/xml";
    }
    debug_return "application/octet-stream";
}

static void free_cache(cache_s *cache) {
    debug_enter();
    if (cache == NULL) {
        return;
    }
    if (cache->data == NULL) {
        goto term;
    }
    for (size_t i = 0; i < cache->capacity; i++) {
        if (cache->data[i].path != NULL) {
            free((char *)cache->data[i].path);
        }
        if (cache->data[i].data != NULL) {
            free((char *)cache->data[i].data);
        }
    }
term:
    if (cache->data != NULL) {
        free(cache->data);
    }
    free(cache);
    cache = NULL;
    debug_return;
}

static inline size_t hash(const char *key) {
    debug_enter();
    size_t hash = 0;
    for (size_t i = 0; key[i] != '\0'; i++) {
        hash = ((hash << 5) - hash) + key[i];
    }
    debug_return hash;
}

static int init_element(cache_element_s *e) {
    debug_enter();
    int rc = 1;
    e->len = 0;
    e->data = NULL;
    e->mime = NULL;
    FILE *f = fopen(e->path, "r");
    if (f == NULL) {
        log_error(cache->log, "Error opening file %s: %s", e->path, strerror(errno));
        goto term;
    }
    if (fseek(f, 0, SEEK_END)) {
        log_error(cache->log, "Error seeking file %s: %s", e->path, strerror(errno));
        goto term;
    }
    e->len = ftell(f);
    e->data = malloc(e->len);
    if (e->data == NULL) {
        log_error(cache->log, "Error allocating %d bytes for cache data: %s", e->len, strerror(errno));
        goto term;
    }
    if (fseek(f, 0, SEEK_SET)) {
        log_error(cache->log, "Error seeking file %s: %s", e->path, strerror(errno));
        goto term;
    }
    if (fread((char *)e->data, 1, e->len, f) != e->len) {
        log_error(cache->log, "Error reading file %s: %s", e->path, strerror(errno));
        goto term;
    }
    fclose(f);
    f = NULL;
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

static int load_dir(cache_s *cache, cache_element_s **list, const char const *base_path, const char const *path) {
    debug_enter();
    if (cache == NULL) {
        debug_return 1;
    }
    debug("cache->log = %p\n", cache->log);
    if (path == NULL) {
        log_error(cache->log, "path is NULL");
        debug_return 1;
    }
    debug("base_path = %s, path = %s\n", base_path, path);
    struct dirent *entry;
    DIR *dp = opendir(path);
    if (dp == NULL) {
        log_error(cache->log, "Error opening directory %s: %s", path, strerror(errno));
        debug_return 1;
    }
    while ((entry = readdir(dp)) != NULL) {
        if (entry->d_name[0] == '.') {
            debug("skipping dot file: %s/%s\n", path, entry->d_name);
            continue;
        }
        debug("processing %s/%s\n", path, entry->d_name);
        char *full_path = malloc(strlen(path) + strlen(entry->d_name) + 2);
        if (full_path == NULL) {
            log_error(cache->log, "Error allocating memory for full path: %s", strerror(errno));
            closedir(dp);
            debug_return 1;
        }
        strcpy(full_path, path);
        strcat(full_path, "/");
        strcat(full_path, entry->d_name);
        struct stat statbuf;
        if (stat(full_path, &statbuf) == -1) {
            log_error(cache->log, "Could not stat file %s: %s", full_path, strerror(errno));
            free(full_path);
            closedir(dp);
            debug_return 1;
        }
        if (S_ISDIR(statbuf.st_mode)) {
            if (load_dir(cache, list, base_path, full_path)) {
                free(full_path);
                closedir(dp);
                debug_return 1;
            }
        } else {
            debug("inserting new cache element for %s\n", full_path);
            cache_element_s *new_element = malloc(sizeof(cache_element_s));
            if (new_element == NULL) {
                free(full_path);
                closedir(dp);
                debug_return 1;
            }
            new_element->path = full_path;
            if (init_element(new_element)) {
                free(full_path);
                closedir(dp);
                debug_return 1;
            }
            new_element->path = strdup(full_path + strlen(base_path));
            if (new_element->path == NULL) {
                log_debug(cache->log, "Failed on strdup: %s", strerror(errno));
            }
            new_element->hash = hash(new_element->path);
            new_element->next = *list;
            *list = new_element;
            cache->count++;
        }
        free(full_path);
    }
    debug("finished processing directory %s\n", path);
    closedir(dp);
    debug_return 0;
}
