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

/**
 * @brief HTTP request methods: CONNECT, DELETE, GET, HEAD, OPTIONS, POST,
 * PUT, TRACE.
 */
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

/**
 * @brief Error codes returned from the request module.
 */
typedef enum request_parse_error_e {
    REQUEST_PARSE_OK = 0,
    REQUEST_PARSE_IO_ERROR = 1,
    REQUEST_PARSE_ERROR = 2,
    REQUEST_PARSE_BAD = 400,
    REQUEST_PARSE_INTERNAL = 500,
    REQUEST_PARSE_NOT_IMPLEMENTED = 501,
} request_parse_error_e;

/**
 * @brief HTTP request types: SIMPLE and FULL, as specified in the HTTP RFC.
 * The SIMPLE type refers to HTTP version 0.9 and FULL refers to version 1.0
 * and later. The SIMPLE type must be GET only and doesn't have a version
 * field.
 */
typedef enum request_type_e {
    REQUEST_TYPE_SIMPLE,
    REQUEST_TYPE_FULL
} request_type_e;

/**
 * @brief Forward reference to http_request_s.
 */
typedef struct request_s http_request_s;

/**
 * @brief Represents an HTTP request. The client, request version,
 * URI, URI fragment (the designator following a "#" in the URI),
 * I/O buffer, URI query variable names and values, headers, 
 * request method and type are tracked.
 */
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

/**
 * @brief Free a request. This closes the connection and frees any memory
 * allocated for the request.
 * @param request Pointer to the request_s structure to free.
 * @return nothing.
 */
extern void request_free(request_s *request);

/** 
 * @brief Establishes a request structure for the given client. The request
 * must be free'd by request_free() when it is no longer needed.
 * @param client The client to establish the request for.
 * @return Returns a pointer to a new request_s structure or NULL on error.
 */
extern request_s *request_get(http_client_s *client);

/**
 * @brief Parses the incoming client request and fills in most of the fields
 * of the given request_s structure.
 * @param request The request structure to parse into.
 * @return Returns a request_parse_error_e error code for the parse.
 */
extern request_parse_error_e request_parse(request_s *request);

#endif // REQUEST_H
