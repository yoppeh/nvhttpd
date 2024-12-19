/**
 * @file request.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief nvhttp request module implementation.
 * @version 0.1.0
 * @date 2024-12-02
 * @copyright Copyright (c) 2024
 */

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "http.h"
#include "log.h"
#include "request.h"

#define MAX_RECV_CHARACTERS 8192
#define URL_VAR_NAME_MAX 128
#define URL_VAR_VALUE_MAX 1024
#define URI_SIZE_MAX 1024
#define BUFFER_SIZE 512

static const int IO_OK = 0;
static const int IO_EOF = -1;
static const int IO_ERROR = -2;
static const int http_version_major_default = 0;
static const int http_version_minor_default = 9;
static const char const *error_str_io = "I/O error";
static const char const *method_str[] = {
    [REQUEST_METHOD_CONNECT] = "CONNECT",
    [REQUEST_METHOD_DELETE] = "DELETE",
    [REQUEST_METHOD_GET] = "GET",
    [REQUEST_METHOD_HEAD] = "HEAD",
    [REQUEST_METHOD_OPTIONS] = "OPTIONS",
    [REQUEST_METHOD_POST] = "POST",
    [REQUEST_METHOD_PUT] = "PUT",
    [REQUEST_METHOD_TRACE] = "TRACE",
};

static int add_header(request_s *request, char *var, char *val);
static request_parse_error_e add_variable(request_s *request, char *var, char *val);
static int io_next(request_s *request);
static int io_peek(request_s *request);
static int get_headers(request_s *request);
static request_parse_error_e get_http_ver(request_s *request);
static request_parse_error_e get_method(request_s *request);
static int get_query(request_s *request);
static request_parse_error_e get_uri(request_s *request);
static request_parse_error_e get_uri_fragment(request_s *request);
static int parse_request(request_s *request);
static request_parse_error_e parse_val(request_s *request, char **val);
static request_parse_error_e parse_var(request_s *request, int separator, char **var);
static int skip_ws(request_s *request);

void request_free(request_s *request) {
    debug_enter();
    if (request == NULL) {
        debug_return;
    }
    if (request->buffer) {
        free(request->buffer);
    }
    if (request->uri) {
        free(request->uri);
    }
    if (request->uri_fragment) {
        free(request->uri_fragment);
    }
    if (request->url_variables) {
        http_variable_s *var = request->url_variables;
        while (var) {
            http_variable_s *next = var->next;
            if (var->var) {
                free(var->var);
            }
            if (var->val) {
                free(var->val);
            }
            free(var);
            var = next;
        }
    }
    if (request->headers) {
        http_variable_s *var = request->headers;
        while (var) {
            http_variable_s *next = var->next;
            if (var->var) {
                free(var->var);
            }
            if (var->val) {
                free(var->val);
            }
            free(var);
            var = next;
        }
    }
    free(request);
    debug_return;
}

request_s *request_get(http_client_s *client) {
    debug_enter();
    http_server_s *server = client->server;
    log_debug(server->log, "getting request for client %s", client->ip);
    request_s *request = malloc(sizeof(request_s));
    if (request == NULL) {
        log_error(server->log, "malloc failed: %s", strerror(errno));
        debug_return NULL;
    }
    memset(request, 0, sizeof(request_s));
    request->buffer = malloc(BUFFER_SIZE);
    if (request->buffer == NULL) {
        log_error(server->log, "malloc failed: %s", strerror(errno));
        free(request);
        debug_return NULL;
    }
    request->client = client;
    request->url_variables = NULL;
    log_debug(server->log, "request setup complete for client %s", client->ip);
    debug_return request;
}

request_parse_error_e request_parse(request_s *request) {
    debug_enter();
    request_parse_error_e res;
    log_s *log = request->client->server->log;
    http_client_s *client = request->client;
    int ch;
    log_debug(log, "parsing request from client %s", client->ip);
    res = get_method(request);
    if (res != REQUEST_PARSE_OK) {
        if (res == REQUEST_PARSE_BAD) {
            debug_return REQUEST_PARSE_NOT_IMPLEMENTED;
        }
        debug_return res;
    }
    // Only accepting GET and HEAD
    if (request->method != REQUEST_METHOD_GET && request->method != REQUEST_METHOD_HEAD) {
        debug_return REQUEST_PARSE_NOT_IMPLEMENTED;
    }
    if ((ch = io_next(request)) < IO_OK) {
        log_error(log, error_str_io);
        debug_return REQUEST_PARSE_IO_ERROR;
    }
    if (!isspace(ch)) {
        log_error(log, "invalid request from client %s: missing whitespace after method", client->ip);
        debug_return REQUEST_PARSE_BAD;
    }
    if ((ch = skip_ws(request)) < IO_OK) {
        log_error(log, error_str_io);
        debug_return REQUEST_PARSE_IO_ERROR;
    }
    if (ch == '\n') {
        log_error(log, "invalid request from client %s: expected URI", client->ip);
        debug_return REQUEST_PARSE_BAD;
    }
    if ((ch = get_uri(request)) != REQUEST_PARSE_OK) {
        debug_return ch;
    }
    if ((ch = io_peek(request)) < IO_OK) {
        log_error(log, error_str_io);
        debug_return REQUEST_PARSE_IO_ERROR;
    }
    if (ch == '?') {
        if ((ch = get_query(request)) != REQUEST_PARSE_OK) {
            log_error(log, "invalid request from client %s", client->ip);
            debug_return ch;
        }
        if ((ch = io_peek(request)) < IO_OK) {
            log_error(log, error_str_io);
            debug_return REQUEST_PARSE_IO_ERROR;
        }
    }
    if (ch == '#') {
        if ((ch = io_next(request)) < IO_OK) {
            log_error(log, error_str_io);
            debug_return REQUEST_PARSE_IO_ERROR;
        }
        if ((ch = get_uri_fragment(request)) != REQUEST_PARSE_OK) {
            debug_return ch;
        }
    }
    request->type = REQUEST_TYPE_FULL;
    if (isspace(ch)) {
        if ((ch = skip_ws(request)) < IO_OK) {
            log_error(log, error_str_io);
            debug_return REQUEST_PARSE_IO_ERROR;
        }
        if (ch == '\n') {
            request->type = REQUEST_TYPE_SIMPLE;
        }
    }
    log_debug(log, "got request type %s from client %s", request->type == REQUEST_TYPE_SIMPLE ? "simple" : "full", client->ip);
    if (request->type == REQUEST_TYPE_SIMPLE) {
        if (request->method != REQUEST_METHOD_GET) {
            log_error(log, "invalid request from client %s, simple request must be GET", client->ip);
            debug_return REQUEST_PARSE_BAD;
        }
        request->http_version_major = http_version_major_default;
        request->http_version_minor = http_version_minor_default;
    } else {
        if ((ch = get_http_ver(request)) != REQUEST_PARSE_OK) {
            debug_return ch;
        }
    }
    // Ignoring headers
    debug_return REQUEST_PARSE_OK;
    if ((ch = io_next(request)) < IO_OK) {
        log_error(log, error_str_io);
        debug_return REQUEST_PARSE_IO_ERROR;
    }
    if (ch != '\r') {
        log_error(log, "invalid request from client %s: expected \\r", client->ip);
        debug_return REQUEST_PARSE_BAD;
    }
    if ((ch = io_next(request)) < IO_OK) {
        log_error(log, error_str_io);
        debug_return REQUEST_PARSE_IO_ERROR;
    }
    if (ch != '\n') {
        log_error(log, "invalid request from client %s: expected \\n", client->ip);
        debug_return REQUEST_PARSE_BAD;
    }
    if ((ch = get_headers(request)) != REQUEST_PARSE_OK) {
        debug_return ch;
    }
    if ((ch = io_next(request)) < IO_OK) {
        log_error(log, error_str_io);
        debug_return REQUEST_PARSE_IO_ERROR;
    }
    if (ch != '\r') {
        log_error(log, "invalid request from client %s: expected \\r", client->ip);
        debug_return REQUEST_PARSE_BAD;
    }
    if ((ch = io_next(request)) < IO_OK) {
        log_error(log, error_str_io);
        debug_return REQUEST_PARSE_IO_ERROR;
    }
    if (ch != '\n') {
        log_error(log, "invalid request from client %s: expected \\n", client->ip);
        debug_return REQUEST_PARSE_BAD;
    }
    debug_return REQUEST_PARSE_OK;
}

static int add_header(request_s *request, char *var, char *val) {
    debug_enter();
    http_variable_s *variable = malloc(sizeof(http_variable_s));
    if (variable == NULL) {
        log_error(request->client->server->log, "malloc failed: %s", strerror(errno));
        debug_return IO_ERROR;
    }
    variable->var = var;
    variable->val = val;
    variable->next = request->headers;
    request->headers = variable;
    debug("added header %s = %s\n", var, val);
    debug_return IO_OK;
}

static request_parse_error_e add_variable(request_s *request, char *var, char *val) {
    debug_enter();
    http_variable_s *variable = malloc(sizeof(http_variable_s));
    if (variable == NULL) {
        log_error(request->client->server->log, "malloc failed: %s", strerror(errno));
        debug_return REQUEST_PARSE_IO_ERROR;
    }
    variable->var = var;
    variable->val = val;
    variable->next = request->url_variables;
    request->url_variables = variable;
    debug("added query variable %s = %s\n", var, val);
    debug_return REQUEST_PARSE_OK;
}

static int io_next(request_s *request) {
    if (request->buffer_index >= request->buffer_len) {
        request->buffer_len = http_read(request->client, request->buffer, BUFFER_SIZE);
        if (request->buffer_len < 0) {
            log_error(request->client->server->log, "recv failed for client %s: %s", request->client->ip, strerror(errno));
            return IO_ERROR;
        }
        if (request->buffer_len == 0) {
            return IO_EOF;
        }
        request->buffer_index = 0;
    }
    return request->buffer[request->buffer_index++];
}

static int io_peek(request_s *request) {
    if (request->buffer_index >= request->buffer_len) {
        request->buffer_len = http_read(request->client, request->buffer, BUFFER_SIZE);
        if (request->buffer_len < 0) {
            log_error(request->client->server->log, "recv failed for client %s: %s", request->client->ip, strerror(errno));
            return IO_ERROR;
        }
        if (request->buffer_len == 0) {
            return IO_EOF;
        }
        request->buffer_index = 0;
    }
    return request->buffer[request->buffer_index];
}

static int get_headers(request_s *request) {
    debug_enter();
    log_s *log = request->client->server->log;
    http_client_s *client = request->client;
    int ch;
    log_debug(log, "parsing headers from client %s", client->ip);
    while (1) {
        if ((ch = io_peek(request)) < IO_OK) {
            log_debug(log, error_str_io);
            debug_return REQUEST_PARSE_IO_ERROR;
        }
        if (ch == '\r') {
            break;
        }
        char *var = NULL;
        if ((ch = parse_var(request, ':', &var)) != REQUEST_PARSE_OK) {
            return ch;
        }
        if (var == NULL) {
            log_debug(log, "No var found from client %s", client->ip);
            return REQUEST_PARSE_BAD;
        }
        if ((ch = io_next(request)) < IO_OK) {
            log_debug(log, error_str_io);
            free(var);
            debug_return REQUEST_PARSE_IO_ERROR;
        }
        if (ch != ':') {
            log_debug(log, "expected :, got %02xh from client %s", ch, client->ip);
            debug_return REQUEST_PARSE_BAD;
        }
        if ((ch = io_next(request)) < IO_OK) {
            log_debug(log, error_str_io);
            debug_return REQUEST_PARSE_IO_ERROR;
        }
        if (ch != ' ') {
            log_debug(log, "expected space, got %02xh from client %s", ch, client->ip);
            debug_return REQUEST_PARSE_BAD;
        }
        char *val = NULL;
        if ((ch = parse_val(request, &val)) != REQUEST_PARSE_OK) {
            free(var);
            return ch;
        }
        if (val == NULL) {
            log_debug(log, "No val found from client %s", client->ip);
            free(var);
            return REQUEST_PARSE_BAD;
        }
        if (add_header(request, var, val) != REQUEST_PARSE_OK) {
            free(var);
            free(val);
            return REQUEST_PARSE_ERROR;
        }
        if ((ch = io_next(request)) < IO_OK) {
            log_debug(log, error_str_io);
            debug_return REQUEST_PARSE_IO_ERROR;
        }
        if (ch != '\r') {
            log_debug(log, "expected \\r from client %s", client->ip);
            debug_return REQUEST_PARSE_BAD;
        }
        if ((ch = io_next(request)) < IO_OK) {
            log_debug(log, error_str_io);
            debug_return REQUEST_PARSE_IO_ERROR;
        }
        if (ch != '\n') {
            log_debug(log, "expected \\n, got %02xh from client %s", ch, client->ip);
            debug_return REQUEST_PARSE_BAD;
        }
    }
    debug_return REQUEST_PARSE_OK;
}

static request_parse_error_e get_http_ver(request_s *request) {
    debug_enter();
    http_client_s *client = request->client;
    log_s *log = client->server->log;
    int ch;
    int i;
    char buffer[16];
    char *minor = NULL;
    log_debug(log, "parsing HTTP version from client %s", client->ip);
    if ((ch = io_next(request)) < IO_OK) {
        log_error(log, error_str_io);
        debug_return REQUEST_PARSE_IO_ERROR;
    }
    if (ch != 'H') {
        log_error(log, "invalid HTTP version from client %s", client->ip);
        debug_return REQUEST_PARSE_BAD;
    }
    if ((ch = io_next(request)) < IO_OK) {
        log_error(log, error_str_io);
        debug_return REQUEST_PARSE_IO_ERROR;
    }
    if (ch != 'T') {
        log_error(log, "invalid HTTP version from client %s", client->ip);
        debug_return REQUEST_PARSE_BAD;
    }
    if ((ch = io_next(request)) < IO_OK) {
        log_error(log, error_str_io);
        debug_return REQUEST_PARSE_IO_ERROR;
    }
    if (ch != 'T') {
        log_error(log, "invalid HTTP version from client %s", client->ip);
        debug_return REQUEST_PARSE_BAD;
    }
    if ((ch = io_next(request)) < IO_OK) {
        log_error(log, error_str_io);
        debug_return REQUEST_PARSE_IO_ERROR;
    }
    if (ch != 'P') {
        log_error(log, "invalid HTTP version from client %s", client->ip);
        debug_return REQUEST_PARSE_BAD;
    }
    if ((ch = io_next(request)) < IO_OK) {
        log_error(log, error_str_io);
        debug_return REQUEST_PARSE_IO_ERROR;
    }
    if (ch != '/') {
        log_error(log, "invalid HTTP version from client %s", client->ip);
        debug_return REQUEST_PARSE_BAD;
    }
    for (i = 0; i < sizeof(buffer) - 1; i++) {
        if ((ch = io_peek(request)) < IO_OK) {
            log_error(log, error_str_io);
            debug_return REQUEST_PARSE_IO_ERROR;
        }
        if (isspace(ch)) {
            buffer[i] = 0;
            break;
        } else if (!isdigit(ch) && ch != '.') {
            log_error(log, "invalid HTTP version from client %s", client->ip);
            debug_return REQUEST_PARSE_BAD;
        } else if (ch == '.') {
            if (minor != NULL) {
                log_error(log, "invalid HTTP version from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            ch = 0;
            minor = &buffer[i + 1];
        }
        if ((ch = io_next(request)) < IO_OK) {
            log_error(log, error_str_io);
            debug_return REQUEST_PARSE_IO_ERROR;
        }
        buffer[i] = ch;
    }
    buffer[i] = 0;
    request->http_version_major = strtol(buffer, NULL, 10);
    request->http_version_minor = strtol(minor, NULL, 10);
    log_debug(log, "HTTP version: %d.%d from client %s", request->http_version_major, request->http_version_minor, client->ip);
    debug_return REQUEST_PARSE_OK;
}

static request_parse_error_e get_method(request_s *request) {
    debug_enter();
    int ch;
    http_client_s *client = request->client;
    log_s *log = client->server->log;
    log_debug(log, "parsing request method from client %s", client->ip);
    if ((ch = io_next(request)) < IO_OK) {
        log_error(log, error_str_io);
        debug_return REQUEST_PARSE_IO_ERROR;
    }
    switch (ch) {
        case 'C':
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'O') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'N') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'N') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'E') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'C') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'T') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            request->method = REQUEST_METHOD_CONNECT;
            break;
        case 'D':
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'E') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'L') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'E') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'T') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'E') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            request->method = REQUEST_METHOD_DELETE;
            break;
        case 'G':
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'E') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'T') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            request->method = REQUEST_METHOD_GET;
            break;
        case 'H':
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'E') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'A') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'D') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            request->method = REQUEST_METHOD_HEAD;
            break;
        case 'O':
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'P') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'T') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'I') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'O') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'N') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'S') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            request->method = REQUEST_METHOD_OPTIONS;
            break;
        case 'P':
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch == 'O') {
                if ((ch = io_next(request)) < IO_OK) {
                    log_error(log, error_str_io);
                    debug_return REQUEST_PARSE_IO_ERROR;
                }
                if (ch != 'S') {
                log_error(log, "invalid method from client %s", client->ip);
                    debug_return REQUEST_PARSE_BAD;
                }
                if ((ch = io_next(request)) < IO_OK) {
                    log_error(log, error_str_io);
                    debug_return REQUEST_PARSE_IO_ERROR;
                }
                if (ch != 'T') {
                log_error(log, "invalid method from client %s", client->ip);
                    debug_return REQUEST_PARSE_BAD;
                }
                request->method = REQUEST_METHOD_POST;
            } else if (ch == 'U') {
                if ((ch = io_next(request)) < IO_OK) {
                    log_error(log, error_str_io);
                    debug_return REQUEST_PARSE_IO_ERROR;
                }
                if (ch != 'T') {
                log_error(log, "invalid method from client %s", client->ip);
                    debug_return REQUEST_PARSE_BAD;
                }
                request->method = REQUEST_METHOD_PUT;
            }
            debug_return REQUEST_PARSE_IO_ERROR;
        case 'T':
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'R') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'A') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'C') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            if ((ch = io_next(request)) < IO_OK) {
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (ch != 'E') {
                log_error(log, "invalid method from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            request->method = REQUEST_METHOD_TRACE;
            break;
        default:
            log_error(log, "invalid method from client %s", client->ip);
            debug_return REQUEST_PARSE_BAD;
    }
    if ((ch = io_peek(request)) < IO_OK) {
        log_error(log, error_str_io);
        debug_return REQUEST_PARSE_IO_ERROR;
    }
    if (!isspace(ch)) {
        log_error(log, "invalid method from client %s", client->ip);
        debug_return REQUEST_PARSE_BAD;
    }
    log_debug(log, "returning method %s (%02x) successfully for client %s", method_str[request->method], request->method, client->ip);
    debug_return REQUEST_PARSE_OK;
}

static int get_query(request_s *request) {
    debug_enter();
    http_client_s *client = request->client;
    log_s *log = client->server->log;
    int ch;
    log_debug(log, "parsing query from URI for client %s", client->ip);
    if ((ch = io_next(request)) == IO_ERROR) {
        debug_return ch;
    }
    if (ch != '?') {
        debug_return IO_OK;
    }
    while (1) {
        if ((ch = io_peek(request)) < IO_OK) {
            debug_return REQUEST_PARSE_IO_ERROR;
        }
        if (isspace(ch) || ch == '#') {
            break;
        }
        char *var = NULL;
        if ((ch = parse_var(request, '=', &var)) != REQUEST_PARSE_OK) {
            return ch;
        }
        if (var == NULL) {
            return REQUEST_PARSE_IO_ERROR;
        }
        if ((ch = io_next(request)) < IO_OK) {
            free(var);
            debug_return REQUEST_PARSE_IO_ERROR;
        }
        if (ch != '=') {
            free(var);
            debug_return REQUEST_PARSE_BAD;
        }
        char *val = NULL;
        if ((ch = parse_val(request, &val)) != REQUEST_PARSE_OK) {
            free(var);
            return ch;
        }
        if (val == NULL) {
            free(var);
            return REQUEST_PARSE_IO_ERROR;
        }
        request_parse_error_e res = add_variable(request, var, val);
        if (res != REQUEST_PARSE_OK) {
            free(var);
            free(val);
            return res;
        }
        if ((ch = io_peek(request)) < IO_OK) {
            debug_return REQUEST_PARSE_IO_ERROR;
        }
        if (ch == '&') {
            if ((ch = io_next(request)) < IO_OK) {
                debug_return REQUEST_PARSE_IO_ERROR;
            }
        }
    }
    debug_return REQUEST_PARSE_OK;
}

static request_parse_error_e get_uri(request_s *request) {
    debug_enter();
    static const char index_html_str[] = "index.html";
    http_client_s *client = request->client;
    log_s *log = client->server->log;
    char *uri = malloc(BUFFER_SIZE);
    size_t uri_size = BUFFER_SIZE;
    size_t uri_len = 0;
    int ch;
    log_info(log, "parsing uri from client %s", client->ip);
    while (1) {
        if ((ch = io_peek(request)) < IO_OK) {
            free(uri);
            log_error(log, error_str_io);
            debug_return REQUEST_PARSE_IO_ERROR;
        }
        if (uri_len >= uri_size) {
            if (uri_size >= URI_SIZE_MAX) {
                free(uri);
                log_error(log, "path too long > %d bytes from client %s", uri_len, client->ip);
                debug_return REQUEST_PARSE_ERROR;
            }
            uri_size <<= 1;
            char *new_uri = realloc(uri, uri_size);
            if (new_uri == NULL) {
                free(uri);
                log_error(log, "realloc failed in client %s: %s", client->ip, strerror(errno));
                debug_return REQUEST_PARSE_ERROR;
            }
            uri = new_uri;
        }
        if (isspace(ch) || ch == '?' || ch == '#') {
            break;
        }
        if (ch == '%') {
            if ((ch = io_next(request)) < IO_OK) {
                free(uri);
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if ((ch = io_next(request)) < IO_OK) {
                free(uri);
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (!isxdigit(ch)) {
                free(uri);
                log_error(log, "invalid hex digit from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            int digit = ch;
            if ((ch = io_peek(request)) < IO_OK) {
                free(uri);
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (!isxdigit(ch)) {
                free(uri);
                log_error(log, "invalid hex digit from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            ch += (digit << 4);
        }
        uri[uri_len++] = ch;
        if ((ch = io_next(request)) < IO_OK) {
            free(uri);
            log_error(log, error_str_io);
            debug_return REQUEST_PARSE_IO_ERROR;
        }
    }
    uri[uri_len] = 0;
    if (uri[uri_len - 1] == '/') {
        if (uri_len + sizeof(index_html_str) > uri_size) {
            uri_size = uri_len + sizeof(index_html_str);
            char *new_uri = realloc(uri, uri_size);
            if (new_uri == NULL) {
                free(uri);
                log_error(log, "realloc failed in client %s: %s", client->ip, strerror(errno));
                debug_return REQUEST_PARSE_ERROR;
            }
            uri = new_uri;
        }
        strcat(uri, index_html_str);
    }
    if ((request->uri = strdup(uri)) == NULL) {
        free(uri);
        log_error(log, "strdup failed in client %s: %s", client->ip, strerror(errno));
        debug_return REQUEST_PARSE_ERROR;
    }
    free(uri);
    log_debug(log, "uri from client %s: %s", client->ip, request->uri);
    debug_return REQUEST_PARSE_OK;
}

static request_parse_error_e get_uri_fragment(request_s *request) {
    debug_enter();
    http_client_s *client = request->client;
    log_s *log = client->server->log;
    char *fragment = malloc(BUFFER_SIZE);
    size_t fragment_size = BUFFER_SIZE;
    size_t fragment_len = 0;
    int ch;
    log_info(log, "parsing uri fragment from client %s", client->ip);
    while (1) {
        if ((ch = io_peek(request)) < IO_OK) {
            free(fragment);
            log_error(log, error_str_io);
            debug_return REQUEST_PARSE_IO_ERROR;
        }
        if (fragment_len >= fragment_size) {
            if (fragment_size >= URI_SIZE_MAX) {
                free(fragment);
                log_error(log, "path too long > %d bytes from client %s", fragment_len, client->ip);
                debug_return REQUEST_PARSE_ERROR;
            }
            fragment_size <<= 1;
            char *new_fragment = realloc(fragment, fragment_size);
            if (new_fragment == NULL) {
                free(fragment);
                log_error(log, "realloc failed in client %s: %s", client->ip, strerror(errno));
                debug_return REQUEST_PARSE_ERROR;
            }
            fragment = new_fragment;
        }
        if (isspace(ch)) {
            break;
        }
        if (ch == '%') {
            if ((ch = io_next(request)) < IO_OK) {
                free(fragment);
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if ((ch = io_next(request)) < IO_OK) {
                free(fragment);
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (!isxdigit(ch)) {
                free(fragment);
                log_error(log, "invalid hex digit from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            int digit = ch;
            if ((ch = io_peek(request)) < IO_OK) {
                free(fragment);
                log_error(log, error_str_io);
                debug_return REQUEST_PARSE_IO_ERROR;
            }
            if (!isxdigit(ch)) {
                free(fragment);
                log_error(log, "invalid hex digit from client %s", client->ip);
                debug_return REQUEST_PARSE_BAD;
            }
            ch += (digit << 4);
        }
        fragment[fragment_len++] = ch;
        if ((ch = io_next(request)) < IO_OK) {
            free(fragment);
            log_error(log, error_str_io);
            debug_return REQUEST_PARSE_IO_ERROR;
        }
    }
    fragment[fragment_len] = 0;
    if ((request->uri_fragment = strdup(fragment)) == NULL) {
        free(fragment);
        log_error(log, "strdup failed from client %s: %s", client->ip, strerror(errno));
        debug_return REQUEST_PARSE_ERROR;
    }
    free(fragment);
    log_debug(log, "uri fragment from client %s: %s", client->ip, request->uri_fragment);
    debug_return REQUEST_PARSE_OK;
}

static request_parse_error_e parse_val(request_s *request, char **val) {
    int ch;
    http_client_s *client = request->client;
    log_s *log = client->server->log;
    char *ret = NULL;
    char *res = malloc(BUFFER_SIZE);
    size_t res_size = BUFFER_SIZE;
    size_t res_len = 0;
    while (1) {
        if ((ch = io_peek(request)) < IO_OK) {
            free(res);
            return REQUEST_PARSE_IO_ERROR;
        }
        if (res_len >= res_size) {
            if (res_size >= URL_VAR_VALUE_MAX) {
                log_error(log, "url variable value too long > %d bytes from client %s", res_len, client->ip);
                free(res);
                return REQUEST_PARSE_ERROR;
            }
            res_size <<= 1;
            char *new_res = realloc(res, res_size);
            if (new_res == NULL) {
                log_error(log, "realloc failed in client %s: %s", client->ip, strerror(errno));
                free(res);
                return REQUEST_PARSE_ERROR;
            }
            res = new_res;
        }
        if (ch == '&' || ch == '\r') {
            break;
        }
        if ((ch = io_next(request)) < IO_OK) {
            free(res);
            return REQUEST_PARSE_IO_ERROR;
        }
        res[res_len++] = ch;
    }
    res[res_len] = 0;
    if ((ret = strdup(res)) == NULL) {
        log_error(log, "strdup failed for client %s: %s", client->ip, strerror(errno));
        free(res);
        return REQUEST_PARSE_ERROR;
    }
    free(res);
    *val = ret;
    return REQUEST_PARSE_OK;
}

static request_parse_error_e parse_var(request_s *request, int separator, char **var) {
    int ch;
    http_client_s *client = request->client;
    log_s *log = client->server->log;
    char *ret = NULL;
    char *res = malloc(BUFFER_SIZE);
    size_t res_size = BUFFER_SIZE;
    size_t res_len = 0;
    while (1) {
        if ((ch = io_peek(request)) < IO_OK) {
            free(res);
            return REQUEST_PARSE_IO_ERROR;
        }
        if (isspace(ch) && separator == '=') {
            free(res);
            return REQUEST_PARSE_BAD;
        }
        if (res_len >= res_size) {
            if (res_size >= URL_VAR_NAME_MAX) {
                log_error(log, "url variable/header name too long > %d bytes from client %s", res_len, client->ip);
                free(res);
                return REQUEST_PARSE_ERROR;
            }
            res_size <<= 1;
            char *new_res = realloc(res, res_size);
            if (new_res == NULL) {
                log_error(log, "realloc failed for client %s: %s", client->ip, strerror(errno));
                free(res);
                return REQUEST_PARSE_ERROR;
            }
            res = new_res;
        }
        if (ch == separator) {
            break;
        }
        if ((ch = io_next(request)) < IO_OK) {
            free(res);
            return REQUEST_PARSE_IO_ERROR;
        }
        res[res_len++] = ch;
    }
    res[res_len] = 0;
    if ((ret = strdup(res)) == NULL) {
        log_error(log, "strdup failed for client %s: %s", client->ip, strerror(errno));
        free(res);
        return REQUEST_PARSE_ERROR;
    }
    free(res);
    *var = ret;
    return REQUEST_PARSE_OK;
}

static int skip_ws(request_s *request) {
    debug_enter();
    while (1) {
        int c = io_peek(request);
        if (c < 0 || !isspace(c) || c == '\n') {
            debug_return c;
        }
        io_next(request);
    }
}
