/**
 * @file logger.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief To use the logger module, include this file and define a global 
 * logger_s. Initialize the logger_s variable appropriately, then use the 
 * macros to log messages. See main.c for an example. The default maximum
 * message size is 2048 bytes.
 * @version 0.1.0
 * @date 2024-02-26
 * @copyright Copyright (c) 2024
 */

#ifndef _LOGGER_H
#define _LOGGER_H

#include <stdio.h>
#include <sys/types.h>

#ifndef LOGGER_QUEUE_LENGTH
#define LOGGER_QUEUE_LENGTH 5
#endif

/**
 * @brief These are the log levels that can be used with the logger_s
 * variable and the logger_write() function. The value in logger_s defines the
 * minimum level that will be logged. The value in logger_write() defines the
 * level of the message being logged. If the level of the message is less than
 * the level in logger_s, the message will not be logged.
 */
typedef enum logger_levels_e {
    /// @brief Only log errors.
    LOGGER_ERROR, 
    /// @brief Log errors and warnings.
    LOGGER_WARN,
    /// @brief Log errors, warnings, and info messages.
    LOGGER_INFO,
    /// @brief Log errors, warnings, info messages, and debug messages.
    LOGGER_DEBUG,
    /// @brief Log everything.
    LOGGER_TRACE,
    LOGGER_ALL = LOGGER_TRACE
} logger_levels_e;

/**
 * @brief The logger_s structure is used to hold the state of the logger. It
 * should be initialized with logger_init() before use, and cleaned up with
 * logger_cleanup() when it is no longer needed.
 */
typedef struct logger_s {
	logger_levels_e log_level;
	pid_t pid;
    pid_t tid;
	const char *app_name;
	size_t app_name_len;
    FILE *fs;
} logger_s;

/**
 * @brief Log a message at the error level.
 */
#define logger_error(logger, format, ...) \
    if ((logger) && (logger)->log_level >= LOGGER_ERROR) \
        logger_write( \
            logger, \
            LOGGER_ERROR, \
            __FILE__, \
            __LINE__, \
            (const char *)(format), \
            ##__VA_ARGS__)
/**
 * @brief Log a message at the warn level.
 */
#define logger_warn(logger, format, ...) \
    if ((logger) && (logger)->log_level >= LOGGER_WARN) \
        logger_write( \
            logger, \
            LOGGER_WARN, \
            __FILE__, \
            __LINE__, \
            (const char *)(format), \
            ##__VA_ARGS__)
/**
 * @brief Log a message at the info level.
 */
#define logger_info(logger, format, ...) \
    if ((logger) && (logger)->log_level >= LOGGER_INFO) \
        logger_write( \
            logger, \
            LOGGER_INFO, \
            __FILE__, \
            __LINE__, \
            (const char *)(format), \
            ##__VA_ARGS__)
/**
 * @brief Log a message at the debug level.
 */
#define logger_debug(logger, format, ...) \
    if ((logger) && (logger)->log_level >= LOGGER_DEBUG) \
        logger_write( \
            logger, \
            LOGGER_DEBUG, \
            __FILE__, \
            __LINE__, \
            (const char *)(format), \
            ##__VA_ARGS__)
/**
 * @brief Log a message at the trace level.
 */
#define logger_srace(logger, format, ...) \
    if ((logger) && (logger)->log_level >= LOGGER_TRACE) \
        logger_write( \
            logger, \
            LOGGER_TRACE, \
            __FILE__, \
            __LINE__, \
            (const char *)(format), \
            ##__VA_ARGS__)

/**
 * @brief Clean up the logger_s variable allocated by logger_init().
 * @param logger Pointer to the logger_s variable to clean up.
 */
extern void logger_cleanup(logger_s *logger);
/**
 * @brief Initialize the logger_s variable.
 * @param level The minimum level of messages to log.
 * @param buffer_size The size of the buffer to use for formatted log messages.
 * The actual buffer size will be larger than this, to allow for the 
 * application name, date/time, pid, tid, source file, line number and level. 
 * This is only the size for the formatted message as passed to logger_write().
 * If this is <=0, the default size of 2048 bytes will be used.
 * @param app_name The name of the application.
 * @param fs The file descriptor to write log messages to.
 * @return logger_s* Pointer to a logger_s variable, ready for use.
 */
extern logger_s *logger_init(logger_levels_e level, 
    const char *app_name,
    FILE *fs);
/**
 * @brief Write a message to the log, if the level of the message is greater
 * than or equal to the level in the logger_s variable.
 * @param logger A pointer to a logger_s initialized by logger_init().
 * @param level The log level for this message.
 * @param source_name The name of the source file where the message is being
 * logged.
 * @param line_number Line number within the source file where the message is
 * being logged.
 * @param format printf style format string.
 * @param ... Arguments to be formatted by the format string.
 */
extern void logger_write(logger_s *logger, 
    logger_levels_e level,  
    const char *source_name, 
    const int line_number, 
    const char *format, 
    ...);

#endif // _LOGGER_H
