#include "system_read_write.h"

#include <stdexcept>
#include <sstream>

std::string readAll(int filed) {
    std::ostringstream result{};

    static int size = 1024 * 128;
    char* buffer = new char[size + 1];
    int number_read = 0;

    while (true) {
        errno = 0;
        int current_number_read = read(filed, buffer + number_read, (size - number_read));
        if (current_number_read < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("Cannot read from given file!");
        }
        if (current_number_read == 0) {
            buffer[number_read] = '\0';
            result << buffer;
            break;
        }
        number_read += current_number_read;
        if (number_read == size) {
            buffer[size] = '\0';
            result << buffer;
        }
    }

    return result.str();
}

void writeAll(int filed, std::string message) {
    int number_written = 0;

    while(number_written < message.length()) {
        errno = 0;
        int current_number_written = write(filed, &(message.c_str()[number_written]), message.length() - number_written);
        if (current_number_written < 0) {
            if (errno != EINTR) throw std::runtime_error("Cannot write to given file!");
        } else {
            number_written += current_number_written;
        }
    }
}

void read_to_buffer(int file, char* buffer, size_t buffer_size) {
    size_t total_number_read = 0;
    ssize_t number_read = 0;

    while (number_read < buffer_size) {
        number_read = read(file, &buffer[total_number_read], buffer_size - total_number_read);
        if (number_read < 0) {
            if (errno != EINTR) throw std::runtime_error("Cannot read from given file!");
        } else if (!number_read) {
            break;
        } else {
            total_number_read += number_read;
        }
    }
};

void write_from_buffer(int file, char* buffer, size_t buffer_size) {
    size_t total_number_written = 0;

    while (total_number_written < buffer_size) {
        ssize_t number_written = write(file, &buffer[total_number_written], buffer_size - total_number_written);
        if (number_written < 0) {
            if (errno != EINTR) throw std::runtime_error("Cannot write to given file!");
        } else {
            total_number_written += number_written;
        }
    }
}
