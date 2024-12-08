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

typedef enum http_request_method_e {
    HTTP_METHOD_CONNECT,
    HTTP_METHOD_DELETE,
    HTTP_METHOD_GET,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_OPTIONS,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_TRACE
} http_request_method_e;

typedef struct request_s http_request_s;

typedef struct request_s {
    http_client_s *client;
    char *http_version;
    char *path;
    char *buffer;
    size_t buffer_len;
    size_t buffer_index;
    http_variable_s *url_variables;
    http_variable_s *headers;
    http_request_method_e method;
} request_s;

extern void request_free(request_s *request);
extern request_s *request_get(http_client_s *client);

#endif // REQUEST_H
