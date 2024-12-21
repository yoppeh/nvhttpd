/**
 * @file option.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Functions for parsing command line options.
 * @version 0.1.0
 * @date 2024-12-20
 * @copyright Copyright (c) 2024
 */

#ifndef _OPTION_H
#define _OPTION_H

#include <stdbool.h>

/**
 * @brief Type of argument an option takes.
 */
typedef enum option_arg_e {
    option_arg_none = 0, // No argument.
    option_arg_required = 1, // Argument required.
    option_arg_optional = 2 // Argument optional.
} option_arg_e;

/**
 * @details Describes an option. Set name and arg_type appropriately, and set
 * value to NULL. validate() will be called any time the corresponding option 
 * is specified on the command line. If validate() returns 0, the option is
 * considered valid. If validate() returns non-zero, the option is considered
 * invalid and option_parse_args() will return 1. The set_missing callback is
 * called for options that are not specified on the command line. The
 * option_set_missing() function should be called after option_parse_args().
 * It will scan all option_t structures and call set_missing() for each one
 * that wasn't specified on the command-line. The supplied set_missing() 
 * function should return 0 on success or 1 on error.
 */
typedef struct option_s {
    const char *name;
    const char *description;
    option_arg_e arg_type;
    const char *value;
    int (*validate)(struct option_s *option);
    bool present;
} option_s;

/**
 * @brief Parse command line arguments into options.
 * @param options a NULL-terminated array of option_s pointers.
 * @param ac argument count, as passed to main.
 * @param av argument vector, as passed to main.
 * @return 0 on success, 1 on failure.
 */
extern int option_parse_args(option_s **options, int ac, char **av);

/**
 * @brief Show help text for options. 
 * @param options a NULL-terminated array of option_s.
 */
extern void option_show_help(option_s **options);

#endif // _OPTION_H
