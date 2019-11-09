#ifndef MYSHELL_REDIRECTSPARSER_H
#define MYSHELL_REDIRECTSPARSER_H

#include <vector>
#include <tuple>
#include "CommandPart.h"

bool isRedirect(CommandPart command) {
    size_t i = 0;
    while (i < command.size() && std::string("0123456789").find(command[i]) != std::string::npos) ++i;

    if (i < command.size() && (command[i] == '<' || command[i] == '>')) ++i;
    else return false;

    if (i < command.size() && command[i] == '&') {
        ++i;
        size_t j = i;
        while (j < command.size() && std::string("0123456789").find(command[j]) != std::string::npos) ++j;
        if (j == i) return false;
    }

    return true;
}

std::tuple<int, int, int> parseRedirect(CommandPart command, int defaultOut = 1, int defaultIn = 0) {
    int from = -1, direction = 0, to = -1;

    size_t i = 0;
    while (i < command.size() && std::string("0123456789").find(command[i]) != std::string::npos) ++i;
    if (i != 0) from = stoi(command.string.substr(0, i));

    if (i < command.size()) {
        if (command[i] == '<') direction = -1;
        else if (command[i] == '>') direction = 1;
        ++i;
    }

    if (i < command.size()) {
        if (command[i] == '&') ++i;
        size_t j = i;
        while (j < command.size() && std::string("0123456789").find(command[j]) != std::string::npos) ++j;
        if (j != i) to = stoi(command.string.substr(i, j));
    }

    if (from == -1) from = direction == -1 ? defaultIn : defaultOut;
    return std::make_tuple(from, direction, to);
}

#endif //MYSHELL_REDIRECTSPARSER_H
