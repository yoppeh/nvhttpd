/**
 * @file log.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Logger implementation.
 * @version 0.1.0
 * @date 2024-11-27
 * @copyright Copyright (c) 2024
 */

#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
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
#define LOG_BUFFER_SIZE 1024

static const char const *LEVELS[] = {
	"ERROR",
	"WARN",
	"INFO",
	"DEBUG",
	"TRACE"
};

typedef struct output_s {
    time_t raw_time;
    const char const *source;
    int line;
    const char const *level;
    char buffer[LOG_BUFFER_SIZE];
    pid_t tid;
    struct output_s *next;
} output_s;

typedef struct log_queue_s {
    pthread_t log_thread;
    output_s *head;
    output_s *tail;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool stop;
} log_queue_s;

static void enqueue(log_queue_s *queue, output_s *output);
static output_s *dequeue(log_queue_s *queue);
void *writer(void *arg);

void log_cleanup(log_s *log) {
    debug_enter();
    if (log != NULL) {
        if (log->queue != NULL) {
            log_queue_s *queue = log->queue;
            pthread_mutex_lock(&queue->mutex);
            queue->stop = true;
            pthread_cond_broadcast(&queue->cond);
            pthread_mutex_unlock(&queue->mutex);
            pthread_join(queue->log_thread, NULL);
            free(queue);
        }
        if (log->fs != stderr && log->fs != stdout && log->fs > 0) {
            fclose(log->fs);
        }
        free(log);
    }
    debug_return;
}

log_s *log_init(log_levels_e level, const char *app_name, FILE *fs) {
    debug_enter();
    size_t len;
    log_queue_s *queue = NULL;
    log_s *log = NULL;
    if (app_name == NULL) {
        app_name = "noname";
    }
    len = strlen(app_name);
    log = malloc(sizeof(log_s) + len + 1);
    if (log == NULL) {
        debug_return NULL;
    }
    queue = malloc(sizeof(log_queue_s));
    if (queue == NULL) {
        free(log);
        debug_return NULL;
    }
	memset(log, 0, sizeof(log_s) + len + 1);
    log->queue = queue;
    queue->head = queue->tail = NULL;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
    queue->stop = false;
	log->log_level = level;
	log->pid = getpid();
	log->app_name_len = len;
    log->app_name = (char *)log + sizeof(log_s);
    log->fs = fs;
	memcpy((char *)log->app_name, (char *)app_name, len);
    pthread_create(&queue->log_thread, NULL, writer, log);
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
    output_s *output = malloc(sizeof(output_s));
    if (output == NULL) {
        perror("malloc failed");
        debug_return;
    }
    // NOTE: This is a Linux specific call. It will not work on other platforms.
    output->tid = syscall(__NR_gettid);
    output->source = source_name;
    output->line = line_number;
    output->level = LEVELS[level];
	time(&output->raw_time);
    va_start(ap, format);
	vsnprintf(output->buffer, LOG_BUFFER_SIZE, format, ap);
	va_end(ap);
    enqueue(log->queue, output);
    debug_return;
}

static void enqueue(log_queue_s *queue, output_s *output) {
    output->next = NULL;
    pthread_mutex_lock(&queue->mutex);
    if (queue->tail != NULL) {
        queue->tail->next = output;
    } else {
        queue->head = output;
    }
    queue->tail = output;
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}

static output_s *dequeue(log_queue_s *queue) {
    pthread_mutex_lock(&queue->mutex);
    while (queue->head == NULL && queue->stop == false) {
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }
    output_s *output = queue->head;
    if (output != NULL) {
        queue->head = output->next;
        if (queue->head == NULL) {
            queue->tail = NULL;
        }
    } else {
        queue->head = queue->tail = NULL;
    }
    pthread_mutex_unlock(&queue->mutex);
    return output;
}

void *writer(void *arg) {
    log_s *log = (log_s *)arg;
    log_queue_s *queue = log->queue;
    while (1) {
        output_s *output = dequeue(queue);
        if (output == NULL) {
            break;
        }
        struct tm tm;
        gmtime_r(&output->raw_time, &tm);
        fprintf(log->fs, "%04i-%02i-%02i %02i:%02i:%02i  %s  % 6i  % 6i  %s  % 6i  %-5s  ",
            tm.tm_year + 1900,
            tm.tm_mon + 1,
            tm.tm_mday,
            tm.tm_hour,
            tm.tm_min,
            tm.tm_sec,
            log->app_name,
            log->pid,
            output->tid,
            output->source,
            output->line,
            output->level);
        fprintf(log->fs, output->buffer);
        fprintf(log->fs, "\n");
        fflush(log->fs);
        free(output);
    }
    return NULL;
}
