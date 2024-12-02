/**
 * @file http.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief nvhttp http module declarations.
 * @version 0.1.0
 * @date 2024-11-28
 * @copyright Copyright (c) 2024
 */

#ifndef HTTP_H
#define HTTP_H

#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "logger.h"

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

typedef enum http_response_code_e {
    HTTP_RESPONSE_200 = 0,
    HTTP_RESPONSE_404 = 1
} http_response_code_e;

typedef struct http_server_s {
    logger_s *logger;
    int fd;
    struct sockaddr_in addr;
} http_server_s;

typedef struct http_client_s {
    http_server_s *server;
    char *ip;
    int fd;
    struct sockaddr_in addr;
    socklen_t addr_len;
} http_client_s;

typedef struct http_request_s {
    http_client_s *client;
    char *path;
    char *buffer;
    size_t buffer_len;
    size_t buffer_index;
    http_request_method_e method;
} http_request_s;

typedef struct http_response_s {
    http_request_s *request;
} http_response_s;

extern http_client_s *http_accept(http_server_s *server);
extern void http_client_close(http_client_s *client);
extern void http_close(http_server_s *server);
extern http_server_s *http_init(logger_s *logger, char *server_ip, int port);
extern void http_request_free(http_request_s *request);
extern http_request_s *http_request_get(http_client_s *client);
extern int http_request_parse(http_request_s *request);
extern char *http_response_header(http_response_code_e code, size_t content_length, const char *mime, const char *additional_headers, size_t *header_len);

#endif // HTTP_H