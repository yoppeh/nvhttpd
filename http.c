/**
 * @file http.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief nvhttp http module implementation.
 * @version 0.1.0
 * @date 2024-11-28
 * @copyright Copyright (c) 2024
 */

#define MAX_RECV_CHARACTERS 8192
#define URL_VAR_NAME_MAX 128
#define URL_VAR_VALUE_MAX 1024
#define PATH_SIZE_MAX 1024
#define BUFFER_SIZE 512

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cache.h"
#include "debug.h"
#include "http.h"
#include "log.h"

volatile sig_atomic_t reload = 0;

http_client_s *http_accept(http_server_s *server) {
    debug_enter();
    http_client_s *client = malloc(sizeof(http_client_s));
    if (client == NULL) {
        log_error(server->log, "malloc failed: %s", strerror(errno));
        debug_return NULL;
    }
    client->addr_len = sizeof(client->addr);
    client->fd = accept(server->fd, (struct sockaddr *)&client->addr, &client->addr_len);
    if (reload == 1) {
        if (cache_load(server->html_path, server->log) != 0) {
            debug_return NULL;
        }
        reload = 0;
    }
    if (client->fd < 0) {
        log_error(server->log, "accept failed: %s", strerror(errno));
        free(client);
        debug_return NULL;
    }
    if (server->ssl_ctx != NULL) {
        debug("server->ssl_ctx = %p\n", server->ssl_ctx);
        client->ssl = SSL_new(server->ssl_ctx);
        SSL_set_fd(client->ssl, client->fd);
        ERR_clear_error();
        if (SSL_accept(client->ssl) <= 0) {
            log_error(server->log, "ssl accept failed: %s", ERR_reason_error_string(ERR_get_error()));
            SSL_free(client->ssl);
            close(client->fd);
            free(client);
            debug_return(NULL);
        }
    } else {
        client->ssl = NULL;
    }
    client->server = server;
    client->ip = inet_ntoa(client->addr.sin_addr);
    debug_return client;
}

void http_client_close(http_client_s *client) {
    debug_enter();
    if (client == NULL) {
        debug("client is null\n");
        debug_return;
    }
    if (client->ssl != NULL) {
        int ret = SSL_shutdown(client->ssl);
        if (ret == 0) {
            ret = SSL_shutdown(client->ssl); // Second call to complete shutdown
        }
        if (ret < 0) {
            int err = SSL_get_error(client->ssl, ret);
            log_error(client->server->log, "SSL_shutdown failed: %s (error code: %d)", 
                      ERR_reason_error_string(ERR_get_error()), err);
        }
        SSL_free(client->ssl); // Always free SSL, even on shutdown failure
        client->ssl = NULL;
    }
    if (client->fd >= 0) {
        debug("closing client->fd\n");
        close(client->fd);
        client->fd = -1; // Prevent double close
    }
    debug("freeing client\n");
    free(client);
    debug_return;
}

void http_close(http_server_s *server) {
    debug_enter();
    if (server == NULL) {
        debug_return;
    }
    if (server->fd >= 0) {
        close(server->fd);
    }
    free(server);
    debug_return;
}

http_server_s *http_init(log_s *log, SSL_CTX *ssl_ctx, const char const *html_path, char *server_ip, int port) {
    debug_enter();
    http_server_s *http = malloc(sizeof(http_server_s));
    if (http == NULL) {
        log_error(log, "malloc failed: %s", strerror(errno));
        debug_return NULL;
    }
    http->html_path = html_path;
    http->ssl_ctx = ssl_ctx;
    http->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (http->fd < 0) {
        log_error(log, "socket failed: %s", strerror(errno));
        free(http);
        debug_return NULL;
    }
    http->addr.sin_family = AF_INET;
    if (strncmp(server_ip, "any", sizeof("any")) == 0) {
        http->addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        if (inet_pton(AF_INET, server_ip, &http->addr.sin_addr) < 0) {
            log_error(log, "inet_pton failed: %s", strerror(errno));
            close(http->fd);
            free(http);
            debug_return NULL;
        }
    }
    http->addr.sin_port = htons(port);
    if (bind(http->fd, (struct sockaddr *)&http->addr, sizeof(http->addr)) < 0) {
        log_error(log, "bind failed: %s", strerror(errno));
        close(http->fd);
        free(http);
        debug_return NULL;
    }
    if (listen(http->fd, 10) < 0) {
        log_error(log, "listen failed: %s", strerror(errno));
        close(http->fd);
        free(http);
        debug_return NULL;
    }
    http->log = log;
    debug_return http;
}

size_t http_read(http_client_s *client, void *buffer, size_t len) {
    size_t size = 0;
    if (client->server->ssl_ctx == NULL) {
        debug("non-ssl reading %d bytes\n", len);
        size = recv(client->fd, buffer, len, 0);
        debug("non-ssl read %d of %d bytes\n", size, len);
    } else {
        debug("ssl reading %d bytes\n", len);
        size = SSL_read(client->ssl, buffer, len);
        if (size < 0) {
            int err = SSL_get_error(client->ssl, size);
            debug("SSL_shutdown failed: %s\n", ERR_reason_error_string(ERR_get_error()));
        }
        debug("ssl read %d of %d bytes\n", size, len);
    }
    return size;
}

size_t http_write(http_client_s *client, const void const *buffer, size_t len) {
    size_t size = 0;
    if (client->server->ssl_ctx == NULL) {
        debug("non-ssl write\n");
        size = send(client->fd, buffer, len, 0);
        debug("non-ssl wrote %d of %d bytes\n", size, len);
    } else {
        debug("ssl writing %d bytes\n", len);
        if (SSL_write_ex(client->ssl, buffer, len, &size) == 1) {
            debug("ssl wrote %d of %d bytes\n", size, len);
        } else {
            debug("ssl write failed\n");
            size = 0;
        }
    }
    return size;
}
