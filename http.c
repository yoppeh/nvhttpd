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

static const int IO_OK = 0;
static const int IO_EOF = -1;
static const int IO_ERROR = -2;

static const char *http_response_code_str[] = {
    "200 OK",
    "404 Not Found"
};

static int io_get_method(http_request_s *request);
static int io_get_path(http_request_s *request);
static int io_next(http_request_s *request);
static int io_parse_method_connect(http_request_s *request);
static int io_parse_method_delete(http_request_s *request);
static int io_parse_method_get(http_request_s *request);
static int io_parse_method_head(http_request_s *request);
static int io_parse_method_options(http_request_s *request);
static int io_parse_method_post_or_put(http_request_s *request);
static int io_parse_method_trace(http_request_s *request);
static int io_peek(http_request_s *request);
static int io_skip_ws(http_request_s *request);

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
    if (request) {
        if (request->buffer) {
            free(request->buffer);
        }
        free(request);
    }
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
    if (http_request_parse(request) != 0) {
        free(request->buffer);
        request->buffer = NULL;
        free(request);
        debug_return NULL;
    }
    debug_return request;
}

int http_request_parse(http_request_s *request) {
    debug_enter();
    if (io_skip_ws(request) != IO_OK) {
        debug_return 1;
    }
    if (io_get_method(request) != IO_OK) {
        debug_return 1;
    }
    if (io_skip_ws(request) != IO_OK) {
        debug_return 1;
    }
    if (io_get_path(request) != IO_OK) {
        debug_return 1;
    }
    while (1) {
        int ch;
        if ((ch = io_next(request)) == IO_EOF) {
            debug_return 0;
        } else if (ch == IO_ERROR) {
            debug_return 1;
        } else if (ch == '\r') {
            if ((ch = io_next(request)) == IO_EOF) {
                debug_return 0;
            } else if (ch == IO_ERROR) {
                debug_return 1;
            } else if (ch == '\n') {
                if ((ch = io_next(request)) == IO_EOF) {
                    debug_return 0;
                } else if (ch == IO_ERROR) {
                    debug_return 1;
                } else if (ch == '\r') {
                    if ((ch = io_next(request)) == IO_EOF) {
                        debug_return 0;
                    } else if (ch == IO_ERROR) {
                        debug_return 1;
                    } else if (ch == '\n') {
                        debug_return 0;
                    }
                }
            }
        }
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

static int io_get_method(http_request_s *request) {
    debug_enter();
    int ch;
    char buffer[8];
    for (int i = 0; i < 8; i++) {
        if ((ch = io_peek(request)) < IO_OK) {
            debug_return IO_ERROR;
        }
        if (isspace(ch)) {
            buffer[i] = 0;
            break;
        }
        ch = io_next(request);
        if (ch < IO_OK) {
            debug_return IO_ERROR;
        }
        buffer[i] = tolower(ch);
    }
    if (memcmp(buffer, "connect", 7) == 0) {
        request->method = HTTP_METHOD_CONNECT;
        debug_return IO_OK;
    } else if (memcmp(buffer, "delete", 6) == 0) {
        request->method = HTTP_METHOD_DELETE;
        debug_return IO_OK;
    } else if (memcmp(buffer, "get", 3) == 0) {
        request->method = HTTP_METHOD_GET;
        debug_return IO_OK;
    } else if (memcmp(buffer, "head", 4) == 0) {
        request->method = HTTP_METHOD_HEAD;
        debug_return IO_OK;
    } else if (memcmp(buffer, "option", 6) == 0) {
        request->method = HTTP_METHOD_OPTIONS;
        debug_return IO_OK;
    } else if (memcmp(buffer, "post", 4) == 0) {
        request->method = HTTP_METHOD_POST;
        debug_return IO_OK;
    } else if (memcmp(buffer, "put", 3) == 0) {
        request->method = HTTP_METHOD_PUT;
        debug_return IO_OK;
    } else if (memcmp(buffer, "trace", 5) == 0) {
        request->method = HTTP_METHOD_TRACE;
        debug_return IO_OK;
    }
    debug_return IO_ERROR;
}

static int io_get_path(http_request_s *request) {
    debug_enter();
    static const char index_html_str[] = "index.html";
    char *path = malloc(BUFFER_SIZE);
    size_t path_size = BUFFER_SIZE;
    size_t path_len = 0;
    int ch;
    while (1) {
        ch = io_peek(request);
        if (ch < IO_OK) {
            free(path);
            debug_return ch;
        }
        if (path_len >= path_size) {
            path_size <<= 1;
            char *new_path = realloc(path, path_size);
            if (new_path == NULL) {
                logger_error(request->client->server->logger, "realloc failed: %s", strerror(errno));
                free(path);
                debug_return IO_ERROR;
            }
            path = new_path;
        }
        if (isspace(ch)) {
            break;
        }
        path[path_len++] = ch;
        ch = io_next(request);
        if (ch < IO_OK) {
            free(path);
            debug_return ch;
        }
    }
    path[path_len] = 0;
    if (path[path_len - 1] == '/') {
        if (path_len + sizeof(index_html_str) > path_size) {
            path_size = path_len + sizeof(index_html_str);
            char *new_path = realloc(path, path_size);
            if (new_path == NULL) {
                logger_error(request->client->server->logger, "realloc failed: %s", strerror(errno));
                free(path);
                debug_return IO_ERROR;
            }
            path = new_path;
        }
        strcat(path, index_html_str);
    }
    if ((request->path = strdup(path)) == NULL) {
        logger_error(request->client->server->logger, "strdup failed: %s", strerror(errno));
        free(path);
        debug_return IO_ERROR;
    }
    free(path);
    debug_return IO_OK;
}

static int io_next(http_request_s *request) {
    debug_enter();
    if (request->buffer_index >= request->buffer_len) {
        request->buffer_len = recv(request->client->fd, request->buffer, BUFFER_SIZE, 0);
        logger_info(request->client->server->logger, "recv: %*s\n", request->buffer_len, request->buffer);
        if (request->buffer_len < 0) {
            logger_error(request->client->server->logger, "recv failed: %s", strerror(errno));
            debug_return IO_ERROR;
        }
        if (request->buffer_len == 0) {
            debug_return IO_EOF;
        }
        request->buffer_index = 0;
    }
    debug_return request->buffer[request->buffer_index++];
}

static int io_peek(http_request_s *request) {
    debug_enter();
    if (request->buffer_index >= request->buffer_len) {
        request->buffer_len = recv(request->client->fd, request->buffer, BUFFER_SIZE, 0);
        logger_info(request->client->server->logger, "recv: %*s\n", request->buffer_len, request->buffer);
        if (request->buffer_len < 0) {
            logger_error(request->client->server->logger, "recv failed: %s", strerror(errno));
            debug_return IO_ERROR;
        }
        if (request->buffer_len == 0) {
            debug_return IO_EOF;
        }
        request->buffer_index = 0;
    }
    debug_return request->buffer[request->buffer_index];
}

static int io_skip_ws(http_request_s *request) {
    debug_enter();
    while (1) {
        int c = io_peek(request);
        if (c < 0) {
            debug_return c;
        }
        if (!isspace(c)) {
            debug_return IO_OK;
        }
        io_next(request);
    }
}
