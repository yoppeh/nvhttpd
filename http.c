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

static int io_next(http_request_s *request);
static int io_peek(http_request_s *request);
static int req_add_header(http_request_s *request, char *var, char *val);
static int req_add_variable(http_request_s *request, char *var, char *val);
static int req_get_headers(http_request_s *request);
static int req_get_http_ver(http_request_s *request);
static int req_get_method(http_request_s *request);
static int req_get_path(http_request_s *request);
static int req_get_variables(http_request_s *request);
static int req_parse(http_request_s *request);
static char *req_parse_url_val(http_request_s *request);
static char *req_parse_url_var(http_request_s *request, int separator);
static int req_skip_ws(http_request_s *request);

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
        if (request->http_version) {
            free(request->http_version);
        }
        if (request->path) {
            free(request->path);
        }
        if (request->url_variables) {
            http_variable_s *var = request->url_variables;
            while (var) {
                http_variable_s *next = var->next;
                free(var->var);
                free(var->val);
                free(var);
                var = next;
            }
        }
        if (request->headers) {
            http_variable_s *var = request->headers;
            while (var) {
                http_variable_s *next = var->next;
                free(var->var);
                free(var->val);
                free(var);
                var = next;
            }
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
    request->url_variables = NULL;
    if (request->buffer == NULL) {
        logger_error(server->logger, "malloc failed: %s", strerror(errno));
        free(request);
        debug_return NULL;
    }
    if (req_parse(request) != 0) {
        free(request->buffer);
        request->buffer = NULL;
        free(request);
        debug_return NULL;
    }
    debug_return request;
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

static int io_next(http_request_s *request) {
    if (request->buffer_index >= request->buffer_len) {
        request->buffer_len = recv(request->client->fd, request->buffer, BUFFER_SIZE, 0);
        if (request->buffer_len < 0) {
            logger_error(request->client->server->logger, "recv failed: %s", strerror(errno));
            return IO_ERROR;
        }
        if (request->buffer_len == 0) {
            return IO_EOF;
        }
        request->buffer_index = 0;
    }
    return request->buffer[request->buffer_index++];
}

static int io_peek(http_request_s *request) {
    if (request->buffer_index >= request->buffer_len) {
        request->buffer_len = recv(request->client->fd, request->buffer, BUFFER_SIZE, 0);
        if (request->buffer_len < 0) {
            logger_error(request->client->server->logger, "recv failed: %s", strerror(errno));
            return IO_ERROR;
        }
        if (request->buffer_len == 0) {
            return IO_EOF;
        }
        request->buffer_index = 0;
    }
    return request->buffer[request->buffer_index];
}

static int req_add_header(http_request_s *request, char *var, char *val) {
    debug_enter();
    http_variable_s *variable = malloc(sizeof(http_variable_s));
    if (variable == NULL) {
        logger_error(request->client->server->logger, "malloc failed: %s", strerror(errno));
        debug_return IO_ERROR;
    }
    variable->var = var;
    variable->val = val;
    variable->next = request->headers;
    request->headers = variable;
    debug("added header %s = %s\n", var, val);
    debug_return IO_OK;
}

static int req_add_variable(http_request_s *request, char *var, char *val) {
    debug_enter();
    http_variable_s *variable = malloc(sizeof(http_variable_s));
    if (variable == NULL) {
        logger_error(request->client->server->logger, "malloc failed: %s", strerror(errno));
        debug_return IO_ERROR;
    }
    variable->var = var;
    variable->val = val;
    variable->next = request->url_variables;
    request->url_variables = variable;
    debug("added url variable %s = %s\n", var, val);
    debug_return IO_OK;
}

static int req_get_headers(http_request_s *request) {
    debug_enter();
    int ch;
    while (1) {
        if ((ch = io_peek(request)) < IO_OK) {
            debug_return ch;
        }
        if (ch == '\r') {
            break;
        }
        char *var = req_parse_url_var(request, ':');
        if (var == NULL) {
            return IO_ERROR;
        }
        if ((ch = io_peek(request)) != ':') {
            debug_return IO_ERROR;
        }
        if ((ch = io_next(request)) < IO_OK) {
            debug_return ch;
        }
        if ((ch = req_skip_ws(request)) < IO_OK) {
            debug_return ch;
        }
        char *val = req_parse_url_val(request);
        if (val == NULL) {
            return IO_ERROR;
        }
        if (req_add_header(request, var, val) != IO_OK) {
            free(var);
            free(val);
            return IO_ERROR;
        }
        if ((ch = io_peek(request)) == '\r') {
            if ((ch = io_next(request)) < IO_OK) {
                debug_return ch;
            }
            if ((ch = io_peek(request)) == '\n') {
                if ((ch = io_next(request)) < IO_OK) {
                    debug_return ch;
                }
            } else if (ch < IO_OK) {
                debug_return ch;
            }
        } else if (ch < IO_OK) {
            debug_return ch;
        }
    }
    debug_return IO_OK;
}

static int req_get_http_ver(http_request_s *request) {
    debug_enter();
    int ch;
    int i;
    char buffer[32];
    for (i = 0; i < 32; i++) {
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
    buffer[i] = 0;
    if (memcmp(buffer, "http/", 5) == 0) {
        if ((request->http_version = strdup(buffer + 5)) == NULL) {
            logger_error(request->client->server->logger, "strdup failed: %s", strerror(errno));
            debug_return IO_ERROR;
        }
        debug("http version: %s\n", request->http_version);
        debug_return IO_OK;
    }
    debug_return IO_ERROR;
}

static int req_get_method(http_request_s *request) {
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
    ch = io_peek(request);
    if (ch < IO_OK) {
        debug_return IO_ERROR;
    } else if (!isspace(ch)) {
        debug_return IO_ERROR;
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
    } else if (memcmp(buffer, "options", 7) == 0) {
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

static int req_get_path(http_request_s *request) {
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
            if (path_size >= PATH_SIZE_MAX) {
                logger_error(request->client->server->logger, "path too long > %d bytes", path_len);
                free(path);
                debug_return IO_ERROR;
            }
            path_size <<= 1;
            char *new_path = realloc(path, path_size);
            if (new_path == NULL) {
                logger_error(request->client->server->logger, "realloc failed: %s", strerror(errno));
                free(path);
                debug_return IO_ERROR;
            }
            path = new_path;
        }
        if (isspace(ch) || ch == '?') {
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

static int req_get_variables(http_request_s *request) {
    debug_enter();
    int ch;
    if ((ch = io_next(request)) == IO_ERROR) {
        debug_return ch;
    }
    if (ch != '?') {
        debug_return IO_OK;
    }
    while (1) {
        if ((ch = io_peek(request)) < IO_OK) {
            debug_return ch;
        }
        if (isspace(ch)) {
            debug_return IO_OK;
        }
        char *var = req_parse_url_var(request, '=');
        if (var == NULL) {
            return IO_ERROR;
        }
        if ((ch = io_peek(request)) != '=') {
            debug_return IO_ERROR;
        }
        if ((ch = io_next(request)) < IO_OK) {
            debug_return ch;
        }
        char *val = req_parse_url_val(request);
        if (val == NULL) {
            return IO_ERROR;
        }
        if (req_add_variable(request, var, val) != IO_OK) {
            free(var);
            free(val);
            return IO_ERROR;
        }
        if ((ch = io_peek(request)) == '&') {
            if ((ch = io_next(request)) < IO_OK) {
                debug_return ch;
            }
        } else if (ch < IO_OK) {
            debug_return ch;
        }
    }
    debug_return IO_OK;
}

static int req_parse(http_request_s *request) {
    debug_enter();
    int ch;
    if (req_skip_ws(request) != IO_OK) {
        debug_return 1;
    }
    if (req_get_method(request) != IO_OK) {
        debug_return 1;
    }
    if (req_skip_ws(request) != IO_OK) {
        debug_return 1;
    }
    if (req_get_path(request) != IO_OK) {
        debug_return 1;
    }
    if ((ch = io_peek(request)) < IO_OK) {
        debug_return 1;
    }
    if (ch == '?') {
        if (req_get_variables(request) != IO_OK) {
            debug_return 1;
        }
    }
    if (req_skip_ws(request) != IO_OK) {
        debug_return 1;
    }
    if ((ch = io_peek(request)) < IO_OK) {
        debug_return 1;
    }
    if (req_get_http_ver(request) != IO_OK) {
        debug_return 1;
    }
    for (size_t i = MAX_RECV_CHARACTERS; i; i--) {
        if ((ch = io_peek(request)) < IO_OK) {
            debug_return 1;
        }
        if (ch == '\r') {
            break;
        }
        if (!isspace(ch)) {
            debug_return 1;
        }
        if (ch = io_next(request) < IO_OK) {
            debug_return 1;
        }
    }
    if ((ch = io_next(request)) < IO_OK) {
        debug_return 1;
    }
    if ((ch = io_next(request)) < IO_OK) {
        debug_return 1;
    }
    if (ch == '\n') {
        if (req_get_headers(request) != IO_OK) {
            debug_return 1;
        }
    }
    if ((ch = io_next(request)) < IO_OK) {
        debug_return 1;
    }
    if (ch != '\r') {
        debug_return 1;
    }
    if ((ch = io_next(request)) < IO_OK) {
        debug_return 1;
    }
    if (ch != '\n') {
        debug_return 1;
    }
    debug_return 0;
}

static char *req_parse_url_val(http_request_s *request) {
    int ch;
    char *ret = NULL;
    char *val = malloc(BUFFER_SIZE);
    size_t val_size = BUFFER_SIZE;
    size_t val_len = 0;
    while (1) {
        if ((ch = io_peek(request)) < IO_OK) {
            free(val);
            return NULL;
        }
        if (val_len >= val_size) {
            if (val_size >= URL_VAR_VALUE_MAX) {
                logger_error(request->client->server->logger, "url variable value too long > %d bytes", val_len);
                free(val);
                return NULL;
            }
            val_size <<= 1;
            char *new_val = realloc(val, val_size);
            if (new_val == NULL) {
                logger_error(request->client->server->logger, "realloc failed: %s", strerror(errno));
                free(val);
                return NULL;
            }
            val = new_val;
        }
        if (ch == '&' || ch == '\r') {
            break;
        }
        if ((ch = io_next(request)) < IO_OK) {
            free(val);
            return NULL;
        }
        val[val_len++] = ch;
    }
    val[val_len] = 0;
    if ((ret = strdup(val)) == NULL) {
        logger_error(request->client->server->logger, "strdup failed: %s", strerror(errno));
        free(val);
        return NULL;
    }
    free(val);
    return ret;
}

static char *req_parse_url_var(http_request_s *request, int separator) {
    int ch;
    char *ret = NULL;
    char *var = malloc(BUFFER_SIZE);
    size_t var_size = BUFFER_SIZE;
    size_t var_len = 0;
    while (1) {
        if ((ch = io_peek(request)) < IO_OK || (isspace(ch) && separator != ':')) {
            free(var);
            return NULL;
        }
        if (var_len >= var_size) {
            if (var_size >= URL_VAR_NAME_MAX) {
                logger_error(request->client->server->logger, "url variable/header name too long > %d bytes", var_len);
                free(var);
                return NULL;
            }
            var_size <<= 1;
            char *new_var = realloc(var, var_size);
            if (new_var == NULL) {
                logger_error(request->client->server->logger, "realloc failed: %s", strerror(errno));
                free(var);
                return NULL;
            }
            var = new_var;
        }
        if (ch == separator) {
            break;
        }
        if ((ch = io_next(request)) < IO_OK) {
            free(var);
            return NULL;
        }
        var[var_len++] = ch;
    }
    var[var_len] = 0;
    if ((ret = strdup(var)) == NULL) {
        logger_error(request->client->server->logger, "strdup failed: %s", strerror(errno));
        free(var);
        return NULL;
    }
    free(var);
    return ret;
}

static int req_skip_ws(http_request_s *request) {
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
