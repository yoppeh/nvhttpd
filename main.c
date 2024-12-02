/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief nvhttpd main program module.
 * @version 0.1.0
 * @date 2024-11-27
 * @copyright Copyright (c) 2024
 */

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cache.h"
#include "config.h"
#include "debug.h"
#include "http.h"
#include "logger.h"

#define log_file_def stdout

static const char response_404_path[] = "/error/404.html";

static const char html_path_def[] = "html";
static const char config_file_def[] = "nvhttpd.conf";
static const int server_port_def = 80;
static const char server_ip_def[] = "any";
static const char server_string_def[] = "nvhttpd";

static logger_levels_e log_level = LOGGER_DEBUG;
static char *html_path = NULL;
static char *config_file = NULL;
static char *server_ip = NULL;
static int server_port = server_port_def;
static logger_s *logger = NULL;
static char *response_headers = NULL;
static char **response_headers_array = NULL;
static size_t response_headers_count = 0;
static size_t response_headers_size = 0;
static size_t response_headers_total = 0;
static char *server_string = NULL;
static FILE *log_file = NULL;
static volatile sig_atomic_t terminate = 0;

static config_error_t config_handler(char *section, char *key, char *value);
static int configure(int ac, char **av);
static void int_handler(int sig);
static int handle_connections(http_server_s *server);
static void *handle_client_request(void *arg);
static void generate_response(char *buffer, size_t *buffer_len, char *path, char *response, size_t response_len);

int main(int argc, char *argv[]) {
    debug_enter();
    http_server_s *server = NULL;
    int rc = 1;
    if (configure(argc, argv) != 0) {
        goto shutdown;
    }
    logger = logger_init(log_level, server_string, log_file);
    if (logger == NULL) {
        printf("logger initialization failed\n");
        goto shutdown;
    }
    logger_info(logger, "starting up server");
    if (cache_init() != 0) {
        logger_error(logger, "cache initialization failed");
        goto shutdown;
    }
    struct sigaction sa;
    sa.sa_handler = int_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        logger_error(logger, "signal initialization failed: %s", strerror(errno));
        goto shutdown;
    }
    server = http_init(logger, server_ip, server_port);
    logger_info(logger, "server listening on port %d", server_port);
    if (server == NULL) {
        goto shutdown;
    }
    rc = handle_connections(server);
shutdown:
    if (logger != NULL) {
        logger_info(logger, "shutting down server");
        logger_cleanup(logger);
    }
    cache_free();
    if (response_headers != NULL) {
        free(response_headers);
    }
    if (response_headers_array != NULL) {
        for (size_t i = 0; i < response_headers_count; i++) {
            if (response_headers_array[i] != NULL) {
                free(response_headers_array[i]);
            }
        }
        free(response_headers_array);
    }
    if (html_path != NULL) {
        free(html_path);
    }
    if (server_ip != NULL) {
        free(server_ip);
    }
    if (server != NULL) {
        http_close(server);
    }
    if (config_file != NULL) {
        free(config_file);
    }
    if (log_file != stderr && log_file != stdout) {
        fclose(log_file);
    }
    debug_return rc;
}

static void *handle_client_request(void *arg) {
    debug_enter();
    char *path = NULL;
    if (arg == NULL) {
        debug_return NULL;
    }
    http_client_s *client = (http_client_s *)arg;
    logger_info(logger, "handling new client connection from %s", client->ip);
    http_request_s *request = http_request_get(client);
    if (request == NULL) {
        goto terminate;
    }
    path = malloc(strlen(html_path) + strlen(request->path) + 1);
    if (path == NULL) {
        logger_error(client->server->logger, "malloc failed: %s", strerror(errno));
        goto terminate;
    }
    memcpy(path, html_path, strlen(html_path));
    memcpy(path + strlen(html_path), request->path, strlen(request->path) + 1);
    logger_info(client->server->logger, "requesting path %s", path);
    cache_element_s *e = cache_find(path);
    if (e != NULL) {
        logger_info(client->server->logger, "returning %s from cache", path);
        size_t header_len = 0;
        char *header = http_response_header(HTTP_RESPONSE_200, e->len, e->mime, response_headers, &header_len);
        send(client->fd, header, header_len, 0);
        send(client->fd, e->data, e->len, 0);
    } else {
        logger_info(client->server->logger, "returning 404", path);
        free(path);
        path = malloc(strlen(html_path) + sizeof(response_404_path));
        memcpy(path, html_path, strlen(html_path));
        memcpy(path + strlen(html_path), response_404_path, sizeof(response_404_path));
        e = cache_find(path);
        size_t header_len = 0;
        char * header = http_response_header(HTTP_RESPONSE_404, e->len, e->mime, response_headers, &header_len);
        send(client->fd, header, header_len, 0);
        send(client->fd, e->data, e->len, 0);
    }
terminate:
    if (path != NULL) {
        free(path);
    }
    http_request_free(request);
    http_client_close(client);
    debug_return NULL;
}

static config_error_t config_handler(char *section, char *key, char *value) {
    debug_enter();
    config_error_t rc = CONFIG_ERROR_NONE;
    if (strcasecmp(section, "server") == 0) {
        if (strcasecmp(key, "port") == 0) {
            server_port = atoi(value);
            printf("server port set to %d\n", server_port);
        } else if (strcasecmp(key, "ip") == 0) {
            if (strcasecmp(value, "any") == 0) {
                server_ip = strdup(server_ip_def);
            } else {
                server_ip = strdup(value);
            }
            if (server_ip == NULL) {
                printf("strdup failed: %s\n", strerror(errno));
                rc = CONFIG_ERROR_NO_MEMORY;
                goto term;
            }
            printf("server ip set to %s\n", server_ip);
        } else if (strcasecmp(key, "html_path") == 0) {
            html_path = strdup(value);
            if (html_path == NULL) {
                printf("strdup failed: %s\n", strerror(errno));
                rc = CONFIG_ERROR_NO_MEMORY;
                goto term;
            }
            printf("html path set to %s\n", html_path);
        } else if (strcasecmp(key, "name") == 0) {
            server_string = strdup(value);
            if (server_string == NULL) {
                printf("strdup failed: %s\n", strerror(errno));
                rc = CONFIG_ERROR_NO_MEMORY;
                goto term;
            }
            printf("server name set to %s\n", server_string);
        }
    } else if (strcasecmp(section, "response-headers") == 0) {
        if (response_headers_array == NULL) {
            response_headers_size = 10;
            response_headers_array = malloc(response_headers_size * sizeof(char *));
            if (response_headers_array == NULL) {
                printf("malloc failed: %s\n", strerror(errno));
                rc = CONFIG_ERROR_NO_MEMORY;
                goto term;
            }
        }
        if (response_headers_count == response_headers_size) {
            response_headers_size <<= 1;
            char **tmp = realloc(response_headers_array, response_headers_size * sizeof(char *));
            if (tmp == NULL) {
                printf("realloc failed: %s\n", strerror(errno));
                rc = CONFIG_ERROR_NO_MEMORY;
                goto term;
            }
            response_headers_array = tmp;
        }
        size_t len = strlen(key) + strlen(value) + 5;
        char *s = malloc(len);
        if (s == NULL) {
            printf("malloc failed: %s\n", strerror(errno));
            rc = CONFIG_ERROR_NO_MEMORY;
            goto term;
        }
        response_headers_total += snprintf(s, len, "%s: %s\r\n", key, value);
        response_headers_array[response_headers_count++] = s;
    } else if (strcasecmp(section, "logging") == 0) {
        if (strcasecmp(key, "level") == 0) {
            if (strcasecmp(value, "error") == 0) {
                log_level = LOGGER_ERROR;
            } else if (strcasecmp(value, "warn") == 0) {
                log_level = LOGGER_WARN;
            } else if (strcasecmp(value, "info") == 0) {
                log_level = LOGGER_INFO;
            } else if (strcasecmp(value, "debug") == 0) {
                log_level = LOGGER_DEBUG;
            } else if (strcasecmp(value, "trace") == 0) {
                log_level = LOGGER_TRACE;
            } else if (strcasecmp(value, "all") == 0) {
                log_level = LOGGER_ALL;
            } else {
                printf("unknown log level %s\n", value);
                rc = CONFIG_ERROR_UNEXPECTED_VALUE;
                goto term;
            }
        } else if (strcasecmp(key, "file") == 0) {
            if (strcasecmp(value, "stdout") == 0) {
                log_file = stdout;
            } else if (strcasecmp(value, "stderr") == 0) {
                log_file = stderr;
            } else {
                log_file = fopen(value, "a");
                if (log_file == NULL) {
                    printf("fopen failed: %s\n", strerror(errno));
                    rc = CONFIG_ERROR_UNEXPECTED_VALUE;
                    goto term;
                } else {
                    printf("logging to file %s\n", value);
                }
            }
        }
    } else {
        printf("unknown section: %s\n", section);
        rc = CONFIG_ERROR_UNRECOGNIZED_SECTION;
    }
term:
    debug_return rc;
}

static int configure(int ac, char **av) {
    int rc = 1;
    if (config_file == NULL) {
        config_file = strdup(config_file_def);
        if (config_file == NULL) {
            printf("strdup failed: %s", strerror(errno));
            goto finish;
        }
    }
    config_error_t config_rc = config_parse(config_file, config_handler);
    if (config_rc != CONFIG_ERROR_NONE) {
        printf("config_parse failed: %s", config_get_error_string(config_rc));
        goto finish;
    }
    if (server_string == NULL) {
        server_string = strdup(server_string_def);
        if (server_string == NULL) {
            printf("strdup failed: %s", strerror(errno));
            goto finish;
        }
    }
    if (log_file == NULL) {
        log_file = log_file_def;
    }
    if (html_path == NULL) {
        html_path = strdup(html_path_def);
        if (html_path == NULL) {
            printf("strdup failed: %s", strerror(errno));
            goto finish;
        }
    }
    if (server_ip == NULL) {
        server_ip = strdup(server_ip_def);
        if (server_ip == NULL) {
            printf("strdup failed: %s", strerror(errno));
            goto finish;
        }
    }
    if (response_headers_array != NULL && response_headers_count > 0) {
        response_headers = malloc(response_headers_total + 1);
        if (response_headers == NULL) {
            printf("malloc failed: %s", strerror(errno));
            goto finish;
        }
        size_t offset = 0;
        for (size_t i = 0; i < response_headers_count; i++) {
            if (response_headers_array[i] != NULL) {
                size_t len = strlen(response_headers_array[i]);
                memcpy(response_headers + offset, response_headers_array[i], len);
                offset += len;
                free(response_headers_array[i]);
            }
        }
        response_headers[offset] = 0;
        free(response_headers_array);
        response_headers_array = NULL;
        response_headers_count = 0;
        response_headers_size = 0;
    }
    rc = 0;
finish:
    debug_return rc;
}

static void int_handler(int sig) {
    (void)sig;
    terminate = 1;
}

static int handle_connections(http_server_s *server) {
    debug_enter();
    int rc = 1;
    while (!terminate) {
        http_client_s *client = http_accept(server);
        pthread_attr_t attr;
        if (pthread_attr_init(&attr) != 0) {
            logger_error(logger, "pthread_attr_init failed: %s", strerror(errno));
            goto shutdown;
        }
        if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0) {
            logger_error(logger, "pthread_attr_setdetachstate failed: %s", strerror(errno));
            goto shutdown;
        }
        pthread_t thread_id;
        pthread_create(&thread_id, &attr, handle_client_request, (void *)client);
        pthread_attr_destroy(&attr);
    }
    rc = 0;
shutdown:
    debug_return rc;
}
