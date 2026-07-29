#ifndef STRING_H
#define STRING_H
#include <stdbool.h>

int strlen(const char *ptr);
int strnlen(const char *ptr, int max);
bool isDigit(char c);
int tonumericdigit(char c);

#endif
