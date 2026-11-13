#define _XOPEN_SOURCE 700

#include "utils.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>

int report_error(const char *format, ...)
{
    va_list arguments;
    fputs("droff: ", stderr);
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    return -1;
}

int format_path(char *destination, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(destination, PATH_MAX, format, arguments);
    va_end(arguments);
    if (length < 0 || length >= PATH_MAX)
        return report_error("path is too long");
    return 0;
}

bool streq(const char* s, const char* s1)
{
    for (;; ++s, ++s1) {
        if (*s != *s1) {
            return false;
        }
        if (*s == 0) {
            return true;
        }
    }
}

bool streq2(const char* s, const char* s1, const char* s2)
{
    return streq(s, s1) || streq(s, s2);
}

