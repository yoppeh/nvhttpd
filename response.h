/**
 * @file response.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief response handling module declarations.
 * @version 0.1.0
 * @date 2024-12-05
 * @copyright Copyright (c) 2024
 */

#ifndef RESPONSE_H
#define RESPONSE_H

#include <stdlib.h>

typedef enum http_response_code_e {
    HTTP_RESPONSE_200 = 0,
    HTTP_RESPONSE_400 = 1,
    HTTP_RESPONSE_404 = 2,
    HTTP_RESPONSE_500 = 3,
    HTTP_RESPONSE_501 = 4,
} http_response_code_e;

typedef struct http_response_s {
    struct request_s *request;
} http_response_s;

extern const char const *response_code_str[];

extern char *http_response_header(http_response_code_e code, size_t content_length, const char *mime, const char *additional_headers, size_t *header_len);

#endif // RESPONSE_H