#ifndef MYSHELL_REDIRECTSPARSER_H
#define MYSHELL_REDIRECTSPARSER_H

#include <vector>
#include <tuple>
#include "CommandPart.h"

bool isRedirect(CommandPart command);

std::tuple<int, int, int> parseRedirect(CommandPart command, int defaultOut = 1, int defaultIn = 0);

#endif //MYSHELL_REDIRECTSPARSER_H
