#include "filef.h"

#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>

static ssize_t write_from_buffer(int file, char* buffer, size_t buffer_size) {
    size_t total_number_written = 0;

    while (total_number_written < buffer_size) {
        ssize_t number_written = write(file, &buffer[total_number_written], buffer_size - total_number_written);
        if (number_written < 0) {
            if (errno != EINTR) return -1;
        } else {
            total_number_written += number_written;
        }
    }

    return total_number_written;
}

// Only prints for %s and %d
int filef(int file_number, char* format, ...) {
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
                ssize_t result = write_from_buffer(file_number, &format[str_start], (i - str_start));
                if (result == -1) return 1;
                str_start = i + 2;
            }

            if (format[i + 1] == 's') {
                char* string = va_arg(list, char*);
                write_from_buffer(file_number, string, strlen(string));
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

                ssize_t result = write_from_buffer(STDOUT_FILENO, &int_string[required_size - size], size);
                if (result == -1) return 1;
            }
        }
        else if (format[i] == '\0') {
            break;
        }
    }

    if (i - str_start > 0) {
        ssize_t result = write_from_buffer(STDOUT_FILENO, &format[str_start], (i - str_start));
        if (result == -1) return 1;
    }

    return 0;
};

