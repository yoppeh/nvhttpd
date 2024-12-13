/**
 * @file request.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief nvhttp request module header.
 * @version 0.1.0
 * @date 2024-12-02
 * @copyright Copyright (c) 2024
 */

#ifndef REQUEST_H
#define REQUEST_H

#include "http.h"

typedef enum request_method_e {
    REQUEST_METHOD_CONNECT,
    REQUEST_METHOD_DELETE,
    REQUEST_METHOD_GET,
    REQUEST_METHOD_HEAD,
    REQUEST_METHOD_OPTIONS,
    REQUEST_METHOD_POST,
    REQUEST_METHOD_PUT,
    REQUEST_METHOD_TRACE
} request_method_e;

typedef enum request_parse_error_e {
    REQUEST_PARSE_OK = 0,
    REQUEST_PARSE_IO_ERROR = 1,
    REQUEST_PARSE_ERROR = 2,
    REQUEST_PARSE_BAD = 400,
    REQUEST_PARSE_INTERNAL = 500,
    REQUEST_PARSE_NOT_IMPLEMENTED = 501,
} request_parse_error_e;

typedef enum request_type_e {
    REQUEST_TYPE_SIMPLE,
    REQUEST_TYPE_FULL
} request_type_e;

typedef struct request_s http_request_s;

typedef struct request_s {
    http_client_s *client;
    int http_version_major;
    int http_version_minor;
    char *uri_fragment;
    char *uri;
    char *buffer;
    size_t buffer_len;
    size_t buffer_index;
    http_variable_s *url_variables;
    http_variable_s *headers;
    request_method_e method;
    request_type_e type;
} request_s;

extern void request_free(request_s *request);
extern request_s *request_get(http_client_s *client);
extern request_parse_error_e request_parse(request_s *request);

#endif // REQUEST_H
