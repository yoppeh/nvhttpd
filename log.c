/**
 * @file log.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Logger implementation.
 * @version 0.1.0
 * @date 2024-04-11
 * @copyright Copyright (c) 2024
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "debug.h"
#include "log.h"

// This is sum of the formatted string for the date and time, the app name, the
// process id, the thread id, the source name, the line number, and the level.
static const size_t LOG_BUFFER_SIZE = 1024;

static const char const *LEVELS[] = {
	"ERROR",
	"WARN",
	"INFO",
	"DEBUG",
	"TRACE"
};

typedef struct output_s {
    FILE *fs;
    char *buffer;
    size_t len;
} output_s;

void log_cleanup(log_s *log) {
    debug_enter();
    if (log != NULL) {
        free(log);
    }
    debug_return;
}

log_s *log_init(log_levels_e level, const char *app_name, FILE *fs) {
    debug_enter();
    size_t len;
    log_s *log = NULL;
    if (app_name == NULL) {
        app_name = "noname";
    }
    len = strlen(app_name);
    log = malloc(sizeof(log_s) + len + 1);
    if (log == NULL) {
        debug_return NULL;
    }
	memset(log, 0, sizeof(log_s) + len + 1);
	log->log_level = level;
	log->pid = getpid();
    // NOTE: This is a Linux specific call. It will not work on other platforms.
    log->tid = syscall(__NR_gettid);
	log->app_name_len = len;
    log->app_name = (char *)log + sizeof(log_s);
    log->fs = fs;
	memcpy((char *)log->app_name, (char *)app_name, len);
	debug_return log;
}

void log_write(log_s *log, log_levels_e level, const char *source_name, const int line_number, const char *format, ...) {
    debug_enter();
    va_list ap;
	time_t raw_time;
	struct tm tm;
	struct tm *tm_ptr = &tm;
    size_t len;
    if (log == NULL) {
        debug_return;
    }
    output_s *output = malloc(sizeof(output_s) + LOG_BUFFER_SIZE);
    if (output == NULL) {
        perror("malloc failed");
        debug_return;
    }
    output->buffer = (char *)output + sizeof(output_s);
	time(&raw_time);
	gmtime_r(&raw_time, tm_ptr);
	va_start(ap, format);
	len = snprintf(output->buffer, 
        LOG_BUFFER_SIZE - 2,
        "%04i-%02i-%02i %02i:%02i:%02i  %s  % 10u  %08x  %s  % 6i  %-5s  ", 
        tm_ptr->tm_year + 1900, tm_ptr->tm_mon + 1, tm_ptr->tm_mday, 
        tm_ptr->tm_hour, tm_ptr->tm_min, tm_ptr->tm_sec, 
        log->app_name, log->pid, log->tid, 
        source_name, line_number, 
        LEVELS[level]);
	len += vsnprintf(output->buffer + len, 
        LOG_BUFFER_SIZE - len - 2, 
        format, 
        ap);
    output->buffer[len++] = '\n';
    output->buffer[len] = 0;
    output->len = len;
    output->fs = log->fs;
	va_end(ap);
    fputs(output->buffer, output->fs);
    fflush(output->fs);
    debug_return;
}
