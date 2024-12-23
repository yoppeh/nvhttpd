/**
 * @file response.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief response handling module implementation.
 * @version 0.1.0
 * @date 2024-12-05
 * @copyright Copyright (c) 2024
 */

#include <string.h>
#include <time.h>

#include "debug.h"
#include "http.h"
#include "request.h"
#include "response.h"

const char const *response_code_str[] = {
    "200 OK",
    "400 Bad Request",
    "404 Not Found",
    "500 Internal Server Error",
    "501 Not Implemented",
};

char *http_response_header(http_response_code_e code, size_t content_length, const char *mime, const char *additional_headers, size_t *header_len) {
    debug_enter();
    time_t rawtime;
    struct tm *timeinfo;
    char date_str[80];
    time(&rawtime);
    timeinfo = gmtime(&rawtime);
    strftime(date_str, sizeof(date_str), "%a, %d %b %Y %H:%M:%S GMT", timeinfo);
    char header[1024];
    *header_len = sprintf(header, "HTTP/1.1 %s\r\nDate: %s\r\nContent-Type: %s\r\nContent-Length: %ld\r\n%s\r\n", response_code_str[code], date_str, mime, content_length, additional_headers);
    debug_return strdup(header);
}
