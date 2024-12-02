/**
 * @file debug.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief debugging tools interface.
 * @version 0.1.0
 * @date 2024-09-30
 * @copyright Copyright (c) 2024
 */

#ifndef DEBUG_H
#define DEBUG_H

#ifdef DEBUG
#include <stdio.h>
extern int indent_level;
extern char *indent_string;
#define debug(...) do { fprintf(stderr, "%.*s%s  %d  ", indent_level, indent_string, __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); } while(0)
#define debug_indent_inc() do { indent_level += 4; if (indent_level > 20) indent_level = 20; } while(0)
#define debug_indent_dec() do { indent_level -= 4; if (indent_level < 0) indent_level = 0; } while(0)
#define debug_enter() do { debug("%s()\n", __PRETTY_FUNCTION__); indent_level += 4; } while(0)
#define debug_return indent_level -= 4; return
#else
#define debug(...)
#define debug_enter()
#define debug_return return
#endif

#endif // DEBUG_H
