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

typedef struct http_variable_s {
    char *var;
    char *val;
    struct http_variable_s *next;
} http_variable_s;

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

extern http_client_s *http_accept(http_server_s *server);
extern void http_client_close(http_client_s *client);
extern void http_close(http_server_s *server);
extern http_server_s *http_init(logger_s *logger, char *server_ip, int port);

#endif // HTTP_H