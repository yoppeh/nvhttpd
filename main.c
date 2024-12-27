/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief nvhttpd main program module.
 * @version 0.1.0
 * @date 2024-11-27
 * @copyright Copyright (c) 2024
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cache.h"
#include "config.h"
#include "debug.h"
#include "http.h"
#include "log.h"
#include "option.h"
#include "request.h"
#include "response.h"

#define log_file_def stdout

const char const *program_name = "nvhttpd";
const char const *program_ver_major = "0";
const char const *program_ver_minor = "0";
const char const *program_ver_revision = "1";

static const char response_400_path[] = "/error/400/index.html";
static const char response_404_path[] = "/error/404/index.html";
static const char response_500_path[] = "/error/500/index.html";
static const char response_501_path[] = "/error/501/index.html";

static const char cfg_filename[] = "nvhttpd.conf";
static const char cfg_filename_primary[] = "/etc/nvhttpd/nvhttpd.conf";
static const char pid_filename_def[] = "/var/run/nvhttpd.pid";
static const char html_path_def[] = "html";
static const int server_port_def = 80;
static const int server_ssl_port_def = 443;
static const char server_ip_def[] = "any";
static const char server_string_def[] = "nvhttpd";

static const char const *strong_ciphers = 
    "ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-CHACHA20-POLY1305:"
    "ECDHE-RSA-CHACHA20-POLY1305:"
    "ECDHE-ECDSA-AES128-GCM-SHA256:"
    "ECDHE-RSA-AES128-GCM-SHA256";

volatile sig_atomic_t terminate = 0;

static option_s option_c = {
    .name = "c", 
    .description = "Specify /full/path/and/filename of config file", 
    .arg_type = option_arg_required, 
    .value = NULL, 
    .validate = NULL, 
    .present = false
};
static option_s option_h = {
    .name = "h",
    .description = "Show this help text",
    .arg_type = option_arg_none,
    .value = NULL,
    .validate = NULL,
    .present = false
};
static option_s option_v = {
    .name = "v",
    .description = "Show program version and exit",
    .arg_type = option_arg_none,
    .value = NULL,
    .validate = NULL,
    .present = false
};
static option_s *options[] = {
    &option_c,
    &option_h,
    &option_v,
    NULL
};

static log_levels_e log_level = LOG_DEBUG;
static char *html_path = NULL;
static char *config_file = NULL;
static char *server_ip = NULL;
static int server_port = 0;
static log_s *log = NULL;
static char *pid_filename = NULL;
static char *response_headers = NULL;
static char **response_headers_array = NULL;
static size_t response_headers_count = 0;
static size_t response_headers_size = 0;
static size_t response_headers_total = 0;
static char *server_string = NULL;
static FILE *log_file = NULL;
static SSL_CTX *ssl_ctx = NULL;
static char *ssl_cert_filename = NULL;
static char *ssl_key_filename = NULL;
static bool ssl_enabled = false;

static config_error_t config_handler(char *section, char *key, char *value);
static int configure(int ac, char **av);
static void *handle_client_request(void *arg);
static int handle_connections(http_server_s *server);
static int init_signal_handlers(void);
static int init_ssl(void);
static void sig_handler_ctlc(int sig);
static void sig_handler_pipe(int sig);
static void sig_handler_reload(int sig);

int main(int argc, char *argv[]) {
    debug_enter();
    int pid_file = -1;
    http_server_s *server = NULL;
    int rc = 1;
    if (option_parse_args(options, argc, argv) != 0) {
        option_h.present = true;
    }
    if (option_v.present) {
        printf("%s %s.%s.%s\n", program_name, program_ver_major, program_ver_minor, program_ver_revision);
        goto shutdown;
    }
    if (option_h.present) {
        option_show_help(options);
        goto shutdown;
    }
    if (option_c.present) {
        config_file = strdup((char *)option_c.value);
    }
    if (configure(argc, argv) != 0) {
        goto shutdown;
    }
    pid_file = open(pid_filename, (O_CREAT | O_WRONLY | O_TRUNC), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (pid_file == -1) {
        fprintf(stderr, "unable to open pid file %s: %s\n", pid_filename, strerror(errno));
        goto shutdown;
    }
    char pid_str[32];
    sprintf(pid_str, "%d", getpid());
    if (write(pid_file, pid_str, strlen(pid_str)) != strlen(pid_str)) {
        fprintf(stderr, "unable to write pid file %s: %s\n", pid_filename, strerror(errno));
    }
    close(pid_file);
    pid_file = -1;
    log = log_init(log_level, server_string, log_file);
    if (log == NULL) {
        fprintf(stderr, "log initialization failed\n");
        goto shutdown;
    }
    log_info(log, "starting up server");
    if (cache_init() != 0) {
        log_error(log, "cache initialization failed");
        goto shutdown;
    }
    if (cache_load(html_path, log) != 0) {
        log_error(log, "cache load failed");
        goto shutdown;
    }
    if (ssl_enabled) {
        if (init_ssl() != 0) {
            goto shutdown;
        }
    } else {
        debug("ssl not enabled\n");
        log_info(log, "ssl disabled");
    }
    if (init_signal_handlers() != 0) {
        goto shutdown;
    }
    server = http_init(log, ssl_ctx, html_path, server_ip, server_port);
    log_info(log, "server listening on port %d", server_port);
    if (server == NULL) {
        goto shutdown;
    }
    rc = handle_connections(server);
shutdown:
    debug("shutting down server with result code %d\n", rc);
    if (log != NULL) {
        log_info(log, "shutting down server with result code %d", rc);
    }
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
    if (log != NULL) {
        log_cleanup(log);
    }
    if (pid_file >= 0) {
        close(pid_file);
    }
    if (pid_filename != NULL) {
        unlink(pid_filename);
        free(pid_filename);
    }
    debug_return rc;
}

static config_error_t config_handler(char *section, char *key, char *value) {
    config_error_t rc = CONFIG_ERROR_NONE;
    if (strcasecmp(section, "server") == 0) {
        if (strcasecmp(key, "port") == 0) {
            server_port = atoi(value);
        } else if (strcasecmp(key, "ip") == 0) {
            if (strcasecmp(value, "any") == 0) {
                server_ip = strdup(server_ip_def);
            } else {
                server_ip = strdup(value);
            }
            if (server_ip == NULL) {
                fprintf(stderr, "strdup failed: %s\n", strerror(errno));
                rc = CONFIG_ERROR_NO_MEMORY;
                goto term;
            }
        } else if (strcasecmp(key, "html_path") == 0) {
            html_path = strdup(value);
            if (html_path == NULL) {
                fprintf(stderr, "failed to allocate html path: %s\n", strerror(errno));
                rc = CONFIG_ERROR_NO_MEMORY;
                goto term;
            }
        } else if (strcasecmp(key, "name") == 0) {
            server_string = strdup(value);
            if (server_string == NULL) {
                fprintf(stderr, "strdup failed: %s\n", strerror(errno));
                rc = CONFIG_ERROR_NO_MEMORY;
                goto term;
            }
        }
    } else if (strcasecmp(section, "response-headers") == 0) {
        if (response_headers_array == NULL) {
            response_headers_size = 10;
            response_headers_array = malloc(response_headers_size * sizeof(char *));
            if (response_headers_array == NULL) {
                fprintf(stderr, "malloc failed: %s\n", strerror(errno));
                rc = CONFIG_ERROR_NO_MEMORY;
                goto term;
            }
        }
        if (response_headers_count == response_headers_size) {
            response_headers_size <<= 1;
            char **tmp = realloc(response_headers_array, response_headers_size * sizeof(char *));
            if (tmp == NULL) {
                fprintf(stderr, "realloc failed: %s\n", strerror(errno));
                rc = CONFIG_ERROR_NO_MEMORY;
                goto term;
            }
            response_headers_array = tmp;
        }
        size_t len = strlen(key) + strlen(value) + 5;
        char *s = malloc(len);
        if (s == NULL) {
            fprintf(stderr, "malloc failed: %s\n", strerror(errno));
            rc = CONFIG_ERROR_NO_MEMORY;
            goto term;
        }
        response_headers_total += snprintf(s, len, "%s: %s\r\n", key, value);
        response_headers_array[response_headers_count++] = s;
    } else if (strcasecmp(section, "logging") == 0) {
        if (strcasecmp(key, "level") == 0) {
            if (strcasecmp(value, "error") == 0) {
                log_level = LOG_ERROR;
            } else if (strcasecmp(value, "warn") == 0) {
                log_level = LOG_WARN;
            } else if (strcasecmp(value, "info") == 0) {
                log_level = LOG_INFO;
            } else if (strcasecmp(value, "debug") == 0) {
                log_level = LOG_DEBUG;
            } else if (strcasecmp(value, "trace") == 0) {
                log_level = LOG_TRACE;
            } else if (strcasecmp(value, "all") == 0) {
                log_level = LOG_ALL;
            } else {
                fprintf(stderr, "unknown log level %s\n", value);
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
                    fprintf(stderr, "fopen failed: %s\n", strerror(errno));
                    rc = CONFIG_ERROR_UNEXPECTED_VALUE;
                    goto term;
                }
            }
        } else if (strcasecmp(key, "pid") == 0) {
            pid_filename = strdup(value);
            if (pid_filename == NULL) {
                fprintf(stderr, "strdup failed: %s\n", strerror(errno));
                rc = CONFIG_ERROR_NO_MEMORY;
                goto term;
            }
        }
    } else if (strcasecmp(section, "ssl") == 0) {
        if (strcasecmp(key, "certificate") == 0) {
            ssl_cert_filename = strdup(value);
        } else if (strcasecmp(key, "key") == 0) {
            ssl_key_filename = strdup(value);
        } else if (strcasecmp(key, "enabled") == 0) {
            if (strcasecmp(value, "true") == 0 || strcasecmp(value, "1") == 0 || strcasecmp(value, "yes") == 0) {
                ssl_enabled = true;
            } else if (strcasecmp(value, "false") == 0 || strcasecmp(value, "0") == 0 || strcasecmp(value, "no") == 0) {
                ssl_enabled = false;
            } else {
                fprintf(stderr, "invalid value for ssl.enabled: %s\n", value);
                rc = CONFIG_ERROR_UNEXPECTED_VALUE;
                goto term;
            }
        } else {
            fprintf(stderr, "unrecognized ssl option: %s\n", key);
            rc = CONFIG_ERROR_UNRECOGNIZED_SECTION;
        }
    } else {
        fprintf(stderr, "unknown section: %s\n", section);
        rc = CONFIG_ERROR_UNRECOGNIZED_SECTION;
    }
term:
    return rc;
}

static int configure(int ac, char **av) {
    debug_enter();
    struct stat buffer;
    int rc = 1;
    if (config_file == NULL) {
        if (stat(cfg_filename_primary, &buffer) == 0) {
            config_file = strdup(cfg_filename_primary);
            if (config_file == NULL) {
                fprintf(stderr, "strdup failed: %s", strerror(errno));
                goto finish;
            }
        } else {
            char *run_path = malloc(strlen(av[0]) + strlen(cfg_filename) + 1);
            if (run_path == NULL) {
                fprintf(stderr, "malloc failed: %s", strerror(errno));
                goto finish;
            }
            strcpy(run_path, av[0]);
            char *last_slash = strrchr(run_path, '/');
            if (last_slash != NULL) {
                *last_slash = '\0';
            }
            strcat(run_path, cfg_filename);
            if (stat(run_path, &buffer) == 0) {
                config_file = strdup(run_path);
                if (config_file == NULL) {
                    fprintf(stderr, "strdup failed: %s", strerror(errno));
                    free(run_path);
                    goto finish;
                }
            }
            free(run_path);
        }
    }
    if (config_file == NULL) {
        fprintf(stderr, "no config file specified and none found, using defaults\n");
        rc = 0;
        goto finish;
    }
    debug("using config file found at %s\n", config_file);
    config_error_t config_rc = config_parse(config_file, config_handler);
    if (config_rc != CONFIG_ERROR_NONE) {
        fprintf(stderr, "config_parse failed: %s", config_get_error_string(config_rc));
        goto finish;
    }
    if (server_string == NULL) {
        server_string = strdup(server_string_def);
        if (server_string == NULL) {
            fprintf(stderr, "strdup failed: %s", strerror(errno));
            goto finish;
        }
    }
    if (log_file == NULL) {
        log_file = log_file_def;
    }
    if (html_path == NULL) {
        html_path = strdup(html_path_def);
        if (html_path == NULL) {
            fprintf(stderr, "strdup failed: %s", strerror(errno));
            goto finish;
        }
    }
    if (pid_filename == NULL) {
        pid_filename = strdup(pid_filename_def);
        if (pid_filename == NULL) {
            fprintf(stderr, "strdup failed: %s", strerror(errno));
            goto finish;
        }
    }
    if (server_ip == NULL) {
        server_ip = strdup(server_ip_def);
        if (server_ip == NULL) {
            fprintf(stderr, "strdup failed: %s", strerror(errno));
            goto finish;
        }
    }
    if (server_port == 0) {
        if (ssl_ctx != NULL) {
            server_port = server_ssl_port_def;
        } else {
            server_port = server_port_def;
        }
    }
    if (response_headers_array != NULL && response_headers_count > 0) {
        response_headers = malloc(response_headers_total + 1);
        if (response_headers == NULL) {
            fprintf(stderr, "malloc failed: %s", strerror(errno));
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

static void *handle_client_request(void *arg) {
    debug_enter();
    char *header = NULL;
    if (arg == NULL) {
        debug_return NULL;
    }
    http_client_s *client = (http_client_s *)arg;
    cache_element_s cache_element;
    cache_element_s *e;
    const char const *path = NULL;
    request_parse_error_e parse_error;
    http_response_code_e code;
    log_s *log = client->server->log;
    log_info(log, "handling new client connection from %s", client->ip);
    request_s *request = request_get(client);
    http_variable_s *var = request->headers;
    if (request == NULL) {
        parse_error = REQUEST_PARSE_INTERNAL;
    } else {
        parse_error = request_parse(request);
    }
    if (parse_error == REQUEST_PARSE_OK) {
        code = HTTP_RESPONSE_200;
        path = request->uri;
    } else {
        switch (parse_error) {
            case REQUEST_PARSE_BAD:
                log_info(log, "returning 400");
                code = HTTP_RESPONSE_400;
                path = response_400_path;
                break;
            case REQUEST_PARSE_NOT_IMPLEMENTED:
                log_info(log, "returning 501");
                code = HTTP_RESPONSE_501;
                path = response_501_path;
                break;
            case REQUEST_PARSE_IO_ERROR:
                goto terminate;
            default:
                log_info(log, "returning 500");
                code = HTTP_RESPONSE_500;
                path = response_500_path;
                break;
        }
    }
    if (path != NULL) {
        if ((e = cache_find(path)) == NULL) {
            log_error(client->server->log, "cache find failed");
            if (code == HTTP_RESPONSE_200) {
                code = HTTP_RESPONSE_404;
                path = response_404_path;
                e = cache_find(path);
            }
        }
    }
    if (e == NULL) {
        e = &cache_element;
        cache_element.data = response_code_str[code];
        cache_element.hash = 0;
        cache_element.len = strlen(cache_element.data);
        cache_element.mime = "text/plain";
    }
    size_t header_len = 0;
    size_t out_len = 0;
    size_t data_len = (request->method == REQUEST_METHOD_GET) ? e->len : 0;
    header = http_response_header(code, data_len, e->mime, response_headers, &header_len);
    out_len = header_len + data_len;
    char *output = malloc(out_len);
    if (output == NULL) {
        log_error(log, "Error allocating %d bytes: %s", out_len, strerror(errno));
        goto terminate;
    }
    memcpy(output, header, header_len);
    if (data_len > 0) {
        memcpy(output + header_len, e->data, data_len);
    }
    debug("sending http response: %*s\n", out_len, output);
    size_t offset = 0;
    while (out_len > 0) {
        debug("out_len = %d, offset = %d\n", out_len, offset);
        size_t sent = http_write(client, output + offset, out_len);
        debug("sent = %d\n", sent);
        if (sent < 1) {
            log_error(log, "Error sending header to client %s: %s", client->ip, strerror(errno));
            goto terminate;
        }
        out_len -= sent;
        offset += sent;
    }
terminate:
    if (header != NULL) {
        free(header);
    }
    request_free(request);
    http_client_close(client);
    debug_return NULL;
}

static int handle_connections(http_server_s *server) {
    debug_enter();
    int rc = 1;
    while (!terminate) {
        http_client_s *client = http_accept(server);
        pthread_attr_t attr;
        if (client != NULL) {
            if (pthread_attr_init(&attr) != 0) {
                log_error(log, "pthread_attr_init failed: %s", strerror(errno));
                continue;
            }
            if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0) {
                log_error(log, "pthread_attr_setdetachstate failed: %s", strerror(errno));
                continue;
            }
            pthread_t thread_id;
            pthread_create(&thread_id, &attr, handle_client_request, (void *)client);
            pthread_attr_destroy(&attr);
        }
    }
    rc = 0;
shutdown:
    debug_return rc;
}

static int init_signal_handlers(void) {
    debug_enter();
    struct sigaction sa;
    sa.sa_handler = sig_handler_ctlc;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        log_error(log, "ctl-c signal initialization failed: %s", strerror(errno));
        debug_return 1;
    }
    sa.sa_handler = sig_handler_pipe;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPIPE, &sa, NULL) == -1) {
        log_error(log, "ctl-c signal initialization failed: %s", strerror(errno));
        debug_return 1;
    }
    sa.sa_handler = sig_handler_reload;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        log_error(log, "reload signal initialization failed: %s", strerror(errno));
        debug_return 1;
    }
    debug_return 0;
}

static int init_ssl(void) {
    debug_enter();
    debug("ssl enabled\n");
    log_info(log, "ssl enabled");
    if (ssl_cert_filename == NULL) {
        log_error(log, "ssl certificate filename not specified");
        debug_return 1;
    }
    if (ssl_key_filename == NULL) {
        log_error(log, "ssl key filename not specified");
        debug_return 1;
    }
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    ERR_clear_error();
    if (!(ssl_ctx = SSL_CTX_new(TLS_server_method()))) {
        log_error(log, "failed to initialize ssl context: %s", ERR_reason_error_string(ERR_get_error()));
        debug_return 1;
    }
    SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
    ERR_clear_error();
    if (!SSL_CTX_set_cipher_list(ssl_ctx, strong_ciphers)) {
        log_error(log, "failed to set strong cipher list: %s", ERR_reason_error_string(ERR_get_error()));
        debug_return 1;
    }
    ERR_clear_error();
    if (SSL_CTX_use_certificate_file(ssl_ctx, ssl_cert_filename, SSL_FILETYPE_PEM) <= 0) {
        log_error(log, "failed to load ssl cert %s: %s", ssl_cert_filename, ERR_reason_error_string(ERR_get_error()));
        debug_return 1;
    }
    ERR_clear_error();
    if (SSL_CTX_use_PrivateKey_file(ssl_ctx, ssl_key_filename, SSL_FILETYPE_PEM) <= 0) {
        log_error(log, "failed to load ssl key %s: %s", ssl_key_filename, ERR_reason_error_string(ERR_get_error()));
        debug_return 1;
    }
    ERR_clear_error();
    if (!SSL_CTX_check_private_key(ssl_ctx)) {
        log_error(log, "private key %s does not match the certificate %s", ssl_key_filename, ssl_cert_filename);
        debug_return 1;
    }
    debug_return 0;
}

/**
 * @brief gracefully handle ctrl-c shutdown.
 */
static void sig_handler_ctlc(int sig) {
    (void)sig;
    terminate = 1;
}

/**
 * @brief some browsers (ie) don't correctly terminate the ssl session, forcing
 * a SIGPIPE on the server. This is to ignore that.
 */
static void sig_handler_pipe(int sig) {
    (void)sig;
}

/**
 * @brief SIGUSER1 initiates a cache reload.
 */
static void sig_handler_reload(int sig) {
    (void)sig;
    reload = 1;
}
