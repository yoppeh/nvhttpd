/**
 * @file log.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief To use the log module, include this file and define a global 
 * log_s. Initialize the log_s variable appropriately, then use the 
 * macros to log messages. See main.c for an example. The default maximum
 * message size is 2048 bytes.
 * @version 0.1.0
 * @date 2024-11-27
 * @copyright Copyright (c) 2024
 */

#ifndef _LOG_H
#define _LOG_H

#include <stdio.h>
#include <sys/types.h>

#ifndef LOG_QUEUE_LENGTH
#define LOG_QUEUE_LENGTH 5
#endif

/**
 * @brief These are the log levels that can be used with the log_s
 * variable and the log_write() function. The value in log_s defines the
 * minimum level that will be logged. The value in log_write() defines the
 * level of the message being logged. If the level of the message is less than
 * the level in log_s, the message will not be logged.
 */
typedef enum log_levels_e {
    /// @brief Only log errors.
    LOG_ERROR, 
    /// @brief Log errors and warnings.
    LOG_WARN,
    /// @brief Log errors, warnings, and info messages.
    LOG_INFO,
    /// @brief Log errors, warnings, info messages, and debug messages.
    LOG_DEBUG,
    /// @brief Log everything.
    LOG_TRACE,
    LOG_ALL = LOG_TRACE
} log_levels_e;

/**
 * @brief The log_s structure is used to hold the state of the log. It
 * should be initialized with log_init() before use, and cleaned up with
 * log_cleanup() when it is no longer needed.
 */
typedef struct log_s {
    struct log_queue_s *queue;
	log_levels_e log_level;
	pid_t pid;
	const char *app_name;
	size_t app_name_len;
    FILE *fs;
} log_s;

/**
 * @brief Log a message at the error level.
 */
#define log_error(log, format, ...) \
    if ((log) && (log)->log_level >= LOG_ERROR) \
        log_write( \
            log, \
            LOG_ERROR, \
            __FILE__, \
            __LINE__, \
            (const char *)(format), \
            ##__VA_ARGS__)
/**
 * @brief Log a message at the warn level.
 */
#define log_warn(log, format, ...) \
    if ((log) && (log)->log_level >= LOG_WARN) \
        log_write( \
            log, \
            LOG_WARN, \
            __FILE__, \
            __LINE__, \
            (const char *)(format), \
            ##__VA_ARGS__)
/**
 * @brief Log a message at the info level.
 */
#define log_info(log, format, ...) \
    if ((log) && (log)->log_level >= LOG_INFO) \
        log_write( \
            log, \
            LOG_INFO, \
            __FILE__, \
            __LINE__, \
            (const char *)(format), \
            ##__VA_ARGS__)
/**
 * @brief Log a message at the debug level.
 */
#define log_debug(log, format, ...) \
    if ((log) && (log)->log_level >= LOG_DEBUG) \
        log_write( \
            log, \
            LOG_DEBUG, \
            __FILE__, \
            __LINE__, \
            (const char *)(format), \
            ##__VA_ARGS__)
/**
 * @brief Log a message at the trace level.
 */
#define log_trace(log, format, ...) \
    if ((log) && (log)->log_level >= LOG_TRACE) \
        log_write( \
            log, \
            LOG_TRACE, \
            __FILE__, \
            __LINE__, \
            (const char *)(format), \
            ##__VA_ARGS__)

/**
 * @brief Clean up the log_s variable allocated by log_init().
 * @param log Pointer to the log_s variable to clean up.
 */
extern void log_cleanup(log_s *log);
/**
 * @brief Initialize the log_s variable.
 * @param level The minimum level of messages to log.
 * @param buffer_size The size of the buffer to use for formatted log messages.
 * The actual buffer size will be larger than this, to allow for the 
 * application name, date/time, pid, tid, source file, line number and level. 
 * This is only the size for the formatted message as passed to log_write().
 * If this is <=0, the default size of 2048 bytes will be used.
 * @param app_name The name of the application.
 * @param fs The file descriptor to write log messages to.
 * @return log_s* Pointer to a log_s variable, ready for use.
 */
extern log_s *log_init(log_levels_e level, 
    const char *app_name,
    FILE *fs);
/**
 * @brief Write a message to the log, if the level of the message is greater
 * than or equal to the level in the log_s variable.
 * @param log A pointer to a log_s initialized by log_init().
 * @param level The log level for this message.
 * @param source_name The name of the source file where the message is being
 * logged.
 * @param line_number Line number within the source file where the message is
 * being logged.
 * @param format printf style format string.
 * @param ... Arguments to be formatted by the format string.
 */
extern void log_write(log_s *log, 
    log_levels_e level,  
    const char *source_name, 
    const int line_number, 
    const char *format, 
    ...);

#endif // _LOG_H
