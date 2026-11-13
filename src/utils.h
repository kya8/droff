#ifndef DROFF_UTILS_H
#define DROFF_UTILS_H

#include <stdbool.h>

int report_error(const char *format, ...);
int format_path(char *destination, const char *format, ...);

bool streq(const char* s, const char* s1);

/*
 * @return s==s1 || s==s2
 */
bool streq2(const char* s, const char* s1, const char* s2);

#endif
