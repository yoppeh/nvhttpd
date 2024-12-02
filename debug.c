/**
 * @file debug.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief debugging tools implementation.
 * @version 0.1.0
 * @date 2024-09-30
 * @copyright Copyright (c) 2024
 */

#include "debug.h"

#ifdef DEBUG
int indent_level = 0;
char *indent_string = "                                                                                ";
#endif
