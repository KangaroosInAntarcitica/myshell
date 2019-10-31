#ifndef MYSHELL_WILDCARDS_H
#define MYSHELL_WILDCARDS_H

#include <string>
#include <vector>
#include "CommandPart.h"

bool testWildCard(std::string str, CommandPart wildCard);
bool isWildCard(CommandPart& part);
std::vector<std::string> expandWildCard (const CommandPart partToParse);

#endif //MYSHELL_WILDCARDS_H
