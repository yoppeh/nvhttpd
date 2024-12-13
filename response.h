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

/**
 * @brief HTTP response codes: 200, 501, etc.
 */
typedef enum http_response_code_e {
    HTTP_RESPONSE_200 = 0,
    HTTP_RESPONSE_400 = 1,
    HTTP_RESPONSE_404 = 2,
    HTTP_RESPONSE_500 = 3,
    HTTP_RESPONSE_501 = 4,
} http_response_code_e;

/**
 * @brief Represents an HTTP response.
 */
typedef struct http_response_s {
    struct request_s *request;
} http_response_s;

/**
 * @brief Table of response code strings mapped to http_response_code_e codes.
 */
extern const char const *response_code_str[];

/**
 * @brief Generates an HTTP response header appropriate for the given 
 * parameters.
 * @param code The response code for the response.
 * @param content_length Length of the content part.
 * @param mime Mime type of the content part.
 * @param additional_headers List of headers to send with the response.
 * @param header_len Contains the size of the returned header.
 * @return Returns a character string containing the header to send to the 
 * client. The string is allocated dynamically and freeing it is the
 * responsibility of the caller.
 */
extern char *http_response_header(http_response_code_e code, size_t content_length, const char *mime, const char *additional_headers, size_t *header_len);

#endif // RESPONSE_H