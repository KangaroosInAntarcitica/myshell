#ifndef MYSHELL_SYSTEM_READ_WRITE_H
#define MYSHELL_SYSTEM_READ_WRITE_H

#include <unistd.h>
#include <errno.h>
#include <string>

std::string readAll(int file);
void writeAll(int filed, std::string message);

void read_to_buffer(int file, char* buffer, size_t buffer_size);
void write_from_buffer(int file, char* buffer, size_t buffer_size);

#endif //MYSHELL_SYSTEM_READ_WRITE_H
