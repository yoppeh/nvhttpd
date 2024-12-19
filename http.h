/**
 * @file HTTP.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief nvHTTP HTTP module declarations.
 * @version 0.1.0
 * @date 2024-11-28
 * @copyright Copyright (c) 2024
 */

#ifndef http_H
#define http_H

#include <netinet/in.h>
#include <openssl/ssl.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "log.h"

/**
 * @brief Represents a variable, either a query parameter, which is passed as 
 * part of the URI, or a header.
 */
typedef struct http_variable_s {
    char *var;
    char *val;
    struct http_variable_s *next;
} http_variable_s;

/**
 * @brief Represents the HTTP server. The socket is tracked for accepting 
 * connections, the log handle for logging output and the address structure.
 */
typedef struct http_server_s {
    log_s *log;
    int fd;
    SSL_CTX *ssl_ctx;
    struct sockaddr_in addr;
} http_server_s;

/**
 * @brief Represents an HTTP client connection. A pointer to the server is 
 * kept, along with the IP the client is coming from, the socket used 
 * to communicate with it and the sockaddr_in info.
 */
typedef struct http_client_s {
    http_server_s *server;
    char *ip;
    int fd;
    SSL *ssl;
    struct sockaddr_in addr;
    socklen_t addr_len;
} http_client_s;

/**
 * @brief Accept a client connection.
 * @param server The HTTP server.
 * @return A pointer to an http_client_s structure representing the client
 * connection. This must be freed with http_client_close(). Returns NULL on 
 * error.
 */
extern http_client_s *http_accept(http_server_s *server);

/**
 * @brief Closes a client connection and cleans up allocated resources.
 * @param client The client connection to close.
 * @return nothing
 */
extern void http_client_close(http_client_s *client);

/**
 * @brief Closes the HTTP server connection.
 * @param server The HTTP server to close.
 * @return nothing
 */
extern void http_close(http_server_s *server);

/**
 * @brief Initializes the HTTP server. The server is setup to listen on the
 * given IP address and port and will use the passed log.
 * @param log The log handle to write to.
 * @param ssl_ctx SSL context, NULL if not using SSL.
 * @param server_ip IP for the server to listen on.
 * @param port Port for the server to listen on.
 * @return Pointer to the http_server_s representing the HTTP connection, or
 * NULL on error. 
 */
extern http_server_s *http_init(log_s *log, SSL_CTX *ssl_ctx, char *server_ip, int port);

extern size_t http_read(http_client_s *client, void *buffer, size_t len);
extern size_t http_write(http_client_s *client, const void const *buffer, size_t len);

#endif // http_H