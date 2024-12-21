/**
 * @file option.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Functions for parsing command line options.
 * @version 0.1.0
 * @date 2024-12-20
 * @copyright Copyright (c) 2024
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "option.h"

extern const char const *program_name;

static option_s *match_option(option_s **options, char *s);

int option_parse_args(option_s **options, int ac, char **av) {
    debug_enter();
    for (int i = 1; i < ac; i++) {
        option_s *option = match_option(options, av[i]);
        if (option == NULL) {
            fprintf(stderr, "Unrecognized option \"%s\"\n", av[i]);
            debug_return 1;
        }
        option->present = true;
        switch (option->arg_type) {
            case option_arg_none:
                break;
            case option_arg_required:
                if (i < ac - 1) {
                    option->value = av[++i];
                } else {
                    debug_return 1;
                }
                break;
            case option_arg_optional:
                if (i < ac - 1) {
                    if (av[i + 1][0] != '-') {
                        option->value = av[++i];
                    }
                }
                break;
        }
        if (option->validate != NULL) {
            int r = option->validate(option);
            if (r != 0) {
                debug_return 1;
            }
        }
    }
    debug_return 0;
}

void option_show_help(option_s **options) {
    debug_enter();
    int fld_width = 0;
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    for (int i = 0; options[i] != NULL; i++) {
        int len = strlen(options[i]->name) + 1;
        if (options[i]->arg_type == option_arg_required) {
            len += 7;
        } else if (options[i]->arg_type == option_arg_optional) {
            len += 9;
        }
        if (len > fld_width) {
            fld_width = len;
        }
    }
    fld_width += 3;
    char *s = malloc(fld_width + 1);
    if (s == NULL) {
        debug_return;
    }
    for (int i = 0; options[i] != NULL; i++) {
        char *sp = s;
        memset(s, '.', fld_width);
        s[fld_width] = '\0';
        int l = strlen(options[i]->name);
        *sp++ = '-';
        memcpy(sp, options[i]->name, l);
        sp += l;
        if (options[i]->arg_type == option_arg_required) {
            memcpy(sp, " value ", 7);
        } else if (options[i]->arg_type == option_arg_optional) {
            memcpy(sp, "[ value] ", 9);
        } else {
            *sp = ' ';
        }
        printf("    %s %s\n", s, options[i]->description);
    }
    debug_return;
}

static option_s *match_option(option_s **options, char *s) {
    debug_enter();
    if (s[0] != '-') {
        return NULL;
    }
    s++;
    for (int i = 0; options[i] != NULL; i++) {
        if (strcmp(options[i]->name, s) == 0) {
            debug("option %s matches %s\n", options[i]->name, s);
            debug_return options[i];
        }
    }
    debug_return NULL;
}
