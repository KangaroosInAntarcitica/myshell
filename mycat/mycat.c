#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <sys/stat.h>

#include "filef.h"

const size_t buffer_char_number = 1024 * 1024;

ssize_t read_to_buffer(int file, char* buffer, size_t buffer_size) {
    size_t total_number_read = 0;
    ssize_t number_read = 0;

    while (number_read < buffer_size) {
        number_read = read(file, &buffer[total_number_read], buffer_size - total_number_read);
        if (number_read < 0) {
            if (errno != EINTR) return -1;
        } else if (!number_read) {
            break;
        } else {
            total_number_read += number_read;
        }
    };

    return total_number_read;
};

ssize_t write_from_buffer(int file, char* buffer, size_t buffer_size) {
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

size_t transform(const char* buffer_from, size_t buffer_from_size, char* buffer_to) {
    static const char* hex_map = "0123456789ABCDEF";

    size_t i_to = 0;
    for (size_t i = 0; i < buffer_from_size; ++i) {
        if (isgraph(buffer_from[i]) || isspace(buffer_from[i])) {
            buffer_to[i_to] = buffer_from[i];
            ++i_to;
        } else {
            buffer_to[i_to] = '\\';
            buffer_to[i_to + 1] = 'x';
            buffer_to[i_to + 2] = hex_map[(unsigned char) buffer_from[i] / 16];
            buffer_to[i_to + 3] = hex_map[(unsigned char) buffer_from[i] % 16];
            i_to += 4;
        }
    }

    return i_to;
}

int copy_file(int file_in, int file_out, char* buffer, size_t buffer_size, int formatHex) {
    ssize_t number_read;

    char* read_buffer = formatHex ? buffer + buffer_size * 3 : buffer;
    while ((number_read = read_to_buffer(file_in, read_buffer, buffer_size))) {
        if (number_read < 0) return -1;
        size_t number_to_write = formatHex ? transform(read_buffer, number_read, buffer) : number_read;
        if (write_from_buffer(file_out, buffer, number_to_write) < 0) return -2;
    };

    return 0;
}

int main(int argc, char** argv) {
    char** filenames = malloc((argc - 1) * sizeof(char*));
    int* files = malloc(sizeof(int) * (argc - 1));
    int filenum = 0;
    int help = 0;
    int formatHex = 0;

    if (!filenames || !files) {
        filef(STDERR_FILENO, "Cannot allocate memory\n");
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h")) {
            help = 1;
        } else if (!strcmp(argv[i], "-A")) {
            formatHex = 1;
        } else if (argv[i][0] == '-') {
            filef(STDERR_FILENO, "Invalid argument: %s\n", argv[i]);
        } else {
            filenames[filenum++] = argv[i];
        }
    }

    if (help) {
        const char* HELP_STR = "Usage: cat [OPTION]... [FILE]...\n"
                               "Concatenate FILE(s) to standard output.\n\n"
                               "\t-A,\tformat unprintable characters as hex codes\n"
                               "\t-h,\tprint the instructions and exit\n";
        filef(STDOUT_FILENO, "%s", HELP_STR);
        return 0;
    }
    if (!filenum) return 0;

    for (int i = 0; i < filenum; ++i) {
        files[i] = open(filenames[i], O_RDONLY);

        struct stat buf;
        stat(filenames[i], &buf);
        if (!S_ISREG(buf.st_mode)) {
            filef(STDERR_FILENO, "File %s is not a regular file.\n", filenames[i]);
            for (int j = 0; j < i; ++j) close(files[i]);
            return 2;
        };

        if (files[i] < 0) {
            filef(STDERR_FILENO, "Cannot open file %s\n", filenames[i]);
            for (int j = 0; j < i; ++j) close(files[i]);
            return 2;
        }
    }

    char* buffer = formatHex ? malloc(4 * buffer_char_number) : malloc(buffer_char_number);
    if (!buffer) {
        filef(STDERR_FILENO, "Cannot allocate memory for copying the files\n");
        for (int i = 0; i < filenum; ++i) close(files[i]);
        return 3;
    }

    for (int i = 0; i < filenum; ++i) {
        int result = copy_file(files[i], STDOUT_FILENO, buffer, buffer_char_number, formatHex);

        if (result == -1) {
            filef(STDERR_FILENO, "Cannot read file %s\n", filenames[i]);
            break;
        } else if (result == -2) {
            filef(STDERR_FILENO, "Cannot write to resulting file\n");
            break;
        }
    }

    for (int i = 0; i < filenum; ++i) close(files[i]);

    free(filenames);
    free(files);
    free(buffer);

    return 0;
}
