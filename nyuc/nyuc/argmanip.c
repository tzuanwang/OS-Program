#include <stdio.h>   
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

char **manipulate_args(int argc, const char *const *argv, int (*const manip)(int)) {
    // malloc() for the overall array in which each element is a string of type char *
    char **copy_args;
    copy_args = malloc((argc + 1) * sizeof(char *)); // (argc + 1) for terminator

    // malloc() for each string character-by-character
    for (int i = 0; i < argc; ++i) {
        const char *pre_arg = argv[i];
        size_t len = strlen(pre_arg);
        char *copy_arg;
        copy_arg = malloc((len + 1) * sizeof(char)); // (len + 1) for terminator

        // character-by-character passing through the manip function
        for (size_t k = 0; k < len; ++k) {
            copy_arg[k] = manip(pre_arg[k]);
        }
        // terminate copy_arg string with NULL
        copy_arg[len] = '\0';
        copy_args[i] = copy_arg;
    }
    // terminate copy_args array with NULL
    copy_args[argc] = NULL;
    return copy_args;
}

// ... means variable number of arguments
void free_copied_args(char **args, ...) {
    va_list list;
    va_start(list, args);

    for (char **curr_arg = args; curr_arg != NULL; curr_arg = va_arg(list, char **)) {
        for (char **c = curr_arg; *c != NULL; ++c) {
            free(*c);
        }
        free(curr_arg);
    }
    va_end(list);
}