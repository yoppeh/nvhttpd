/**
 * @file http.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief nvhttp http module implementation.
 * @version 0.1.0
 * @date 2024-11-28
 * @copyright Copyright (c) 2024
 */

#define BUFFER_SIZE 1024

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "debug.h"
#include "http.h"
#include "logger.h"

static const char *http_response_code_str[] = {
    "200 OK",
    "404 Not Found"
};

http_client_s *http_accept(http_server_s *server) {
    debug_enter();
    http_client_s *client = malloc(sizeof(http_client_s));
    if (client == NULL) {
        logger_error(server->logger, "malloc failed: %s", strerror(errno));
        debug_return NULL;
    }
    client->server = server;
    client->addr_len = sizeof(client->addr);
    client->fd = accept(server->fd, (struct sockaddr *)&client->addr, &client->addr_len);
    client->ip = inet_ntoa(client->addr.sin_addr);
    if (client->fd < 0) {
        logger_error(server->logger, "accept failed: %s", strerror(errno));
        free(client);
        debug_return NULL;
    }
    debug_return client;
}

void http_client_close(http_client_s *client) {
    debug_enter();
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

http_server_s *http_init(logger_s *logger, char *server_ip, int port) {
    debug_enter();
    http_server_s *http = malloc(sizeof(http_server_s));
    if (http == NULL) {
        logger_error(logger, "malloc failed: %s", strerror(errno));
        debug_return NULL;
    }
    http->logger = logger;
    http->fd = socket(AF_INET, SOCK_STREAM, 0);
    http->addr.sin_family = AF_INET;
    if (strcmp(server_ip, "any") == 0) {
        http->addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        if (inet_pton(AF_INET, server_ip, &http->addr.sin_addr) < 0) {
            logger_error(logger, "inet_pton failed: %s", strerror(errno));
            free(http);
            debug_return NULL;
        }
    }
    http->addr.sin_port = htons(port);
    if (bind(http->fd, (struct sockaddr *)&http->addr, sizeof(http->addr)) < 0) {
        logger_error(logger, "bind failed: %s", strerror(errno));
        free(http);
        debug_return NULL;
    }
    if (listen(http->fd, 10) < 0) {
        logger_error(logger, "listen failed: %s", strerror(errno));
        free(http);
        debug_return NULL;
    }
    debug_return http;
}

void http_request_free(http_request_s *request) {
    debug_enter();
    free(request->buffer);
    free(request);
    debug_return;
}

http_request_s *http_request_get(http_client_s *client) {
    debug_enter();
    http_server_s *server = client->server;
    http_request_s *request = malloc(sizeof(http_request_s));
    if (request == NULL) {
        logger_error(server->logger, "malloc failed: %s", strerror(errno));
        debug_return NULL;
    }
    request->client = client;
    request->buffer = malloc(BUFFER_SIZE);
    if (request->buffer == NULL) {
        logger_error(server->logger, "malloc failed: %s", strerror(errno));
        free(request);
        debug_return NULL;
    }
    request->buffer_len = recv(client->fd, request->buffer, BUFFER_SIZE, 0);
    if (request->buffer_len < 0) {
        logger_error(server->logger, "recv failed: %s", strerror(errno));
        free(request->buffer);
        free(request);
        debug_return NULL;
    }
    if (http_request_parse(request) != 0) {
        free(request->buffer);
        free(request);
        debug_return NULL;
    }
    debug_return request;
}

int http_request_parse(http_request_s *request) {
    debug_enter();
    static const char index_html_str[] = "index.html";
    char *start;
    char *req = request->buffer;
    while (*req && isspace(*req)) {
        req++;
    }
    if (*req == 0) {
        debug_return 0;
    }
    if (memcmp(req, "CONNECT", 7) == 0) {
        request->method = HTTP_METHOD_CONNECT;
        req += 7;
    } else if (memcmp(req, "DELETE", 6) == 0) {
        request->method = HTTP_METHOD_DELETE;
        req += 6;
    } else if (memcmp(req, "GET", 3) == 0) {
        request->method = HTTP_METHOD_GET;
        req += 3;
    } else if (memcmp(req, "HEAD", 4) == 0) {
        request->method = HTTP_METHOD_HEAD;
        req += 4;
    } else if (memcpy(req, "OPTIONS", 7) == 0) {
        request->method = HTTP_METHOD_OPTIONS;
        req += 7;
    } else if (memcmp(req, "POST", 4) == 0) {
        request->method = HTTP_METHOD_POST;
        req += 4;
    } else if (memcmp(req, "PUT", 3) == 0) {
        request->method = HTTP_METHOD_PUT;
        req += 3;
    } else if (memcmp(req, "TRACE", 5) == 0) {
        request->method = HTTP_METHOD_TRACE;
        req += 5;
    } else {
        debug_return 1;
    }
    while (*req && isspace(*req)) {
        req++;
    }
    start = req;
    while (*req && !isspace(*req) && *req != '?') {
        req++;
    }
    if (*req == 0) {
        debug_return 1;
    }
    request->path = malloc((req - start) + sizeof(index_html_str));
    if (request->path == NULL) {
        logger_error(request->client->server->logger, "malloc failed: %s", strerror(errno));
        debug_return 1;
    }
    memcpy(request->path, start, req - start);
    request->path[req - start] = 0;
    if (request->path[req - start - 1] == '/') {
        memcpy(request->path + (req - start), index_html_str, sizeof(index_html_str));
    }
    debug_return 0;
}

char *http_response_header(http_response_code_e code, size_t content_length, const char *mime, const char *additional_headers, size_t *header_len) {
    debug_enter();
    time_t rawtime;
    struct tm *timeinfo;
    char date_str[80];
    time(&rawtime);
    timeinfo = gmtime(&rawtime);
    strftime(date_str, sizeof(date_str), "%a, %d %b %Y %H:%M:%S GMT", timeinfo);
    char header[1024];
    *header_len = sprintf(header, "HTTP/1.1 %s\r\nDate: %s\r\nContent-Type: %s\r\nContent-Length: %ld\r\n%s\r\n", http_response_code_str[code], date_str, mime, content_length, additional_headers);
    debug_return strdup(header);
}
