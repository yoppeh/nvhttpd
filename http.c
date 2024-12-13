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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "http.h"
#include "log.h"

http_client_s *http_accept(http_server_s *server) {
    debug_enter();
    http_client_s *client = malloc(sizeof(http_client_s));
    if (client == NULL) {
        log_error(server->log, "malloc failed: %s", strerror(errno));
        debug_return NULL;
    }
    client->addr_len = sizeof(client->addr);
    client->fd = accept(server->fd, (struct sockaddr *)&client->addr, &client->addr_len);
    if (client->fd < 0) {
        log_error(server->log, "accept failed: %s", strerror(errno));
        free(client);
        debug_return NULL;
    }
    client->server = server;
    client->ip = inet_ntoa(client->addr.sin_addr);
    debug_return client;
}

void http_client_close(http_client_s *client) {
    debug_enter();
    if (client == NULL) {
        debug_return;
    }
    if (client->fd >= 0) {
        close(client->fd);
    }
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

http_server_s *http_init(log_s *log, char *server_ip, int port) {
    debug_enter();
    http_server_s *http = malloc(sizeof(http_server_s));
    if (http == NULL) {
        log_error(log, "malloc failed: %s", strerror(errno));
        debug_return NULL;
    }
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
