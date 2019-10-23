#ifndef MYCAT_FILEF_H
#define MYCAT_FILEF_H

#include <unistd.h>
#include <string.h>
#include <stdarg.h>

// Only prints for %s and %d
static void filef(int file_number, char* format, ...) {
    va_list list;
    va_start(list, 0);

    size_t str_start = 0;
    size_t i = 0;
    for (;;++i) {
        if (format[i] == '\0') break;

        if (format[i] == '%') {
            if (format[i + 1] != 'd' && format[i + 1] != 's') {
                continue;
            }

            // print the text before
            if (i - str_start > 0) {
                write(file_number, &format[str_start], (i - str_start));
                str_start = i + 2;
            }

            if (format[i + 1] == 's') {
                char* string = va_arg(list, char*);
                write(file_number, string, strlen(string));
            } else if (format[i + 1] == 'd') {
                int integer = va_arg(list, int);
                const int required_size = 20;
                char int_string[required_size];
                const char char_map[] = "0123456789";
                unsigned char sign = integer < 0;
                int size = 0;
                integer = integer < 0 ? -integer : integer;

                do {
                    int_string[required_size - 1 - size++] = char_map[integer % 10];
                    integer /= 10;
                } while (integer != 0);

                if (sign) {
                    int_string[required_size - 1 - size++] = '-';
                }

                write(STDOUT_FILENO, &int_string[required_size - size], size);
            }
        }
        else if (format[i] == '\0') {
            break;
        }
    }

    if (i - str_start > 0) {
        write(STDOUT_FILENO, &format[str_start], (i - str_start));
    }
};

#endif //MYCAT_FILEF_H
