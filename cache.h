/**
 * @file cache.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief cache module declarations.
 * @version 0.1.0
 * @date 2024-11-30
 * @copyright Copyright (c) 2024
 */

#ifndef CACHE_H
#define CACHE_H

/**
 * @brief Represents an element in the cache. The hash is the FULL hash of 
 * the key, meaning it is the result of the hash function applied to the entire
 * key and not adjusted for the capacity of the cache. The key is a pointer
 * to the key string itself, and the value is the value associated with the 
 * key. The value is never touched by the cache code--it is entirely the
 * responsibility of the user.
 */
typedef struct cache_element_s {
    size_t hash;
    size_t len;
    const char *path;
    const char *mime;
    const char *data;
} cache_element_s;

/**
 * @brief Finds an element in the cache.
 * @param path The path to search for.
 * @return cache_element_s* A pointer to the element in the cache
 * containing the path, or NULL if the path is not found.
 */
extern cache_element_s *cache_find(const char *path);

/**
 * @brief Frees the cache. 
 * @return void 
 */
extern void cache_free(void);

/**
 * @brief Initializes the cache.
 * @return 0 on success, 1 on no memory.
 */
extern int cache_init(void);

#endif // CACHE_H