#ifndef MYSHELL_COMMANDPART_H
#define MYSHELL_COMMANDPART_H

#include <string>
#include <vector>
#include <tuple>
#include <sstream>

struct CommandPart {
    std::string string;
    std::vector<bool> escaped;
    char quotes = 0;

    CommandPart() = default;
    CommandPart(char* string, char escape='\\'):
        CommandPart(std::string(string), escape) {}
    CommandPart(const std::string string, char escape='\\');
    size_t size();
    bool empty();
    char& operator[](size_t i);

    // entering - not escaped
    bool includesEntering(char c);
    size_t findEntering(char c);
    CommandPart subPart(size_t start, size_t end=std::string::npos);
    std::vector<CommandPart> splitEntering(char c);
    std::vector<CommandPart> splitCommand(char separator=' ', char normalQuotes='"', char escapeQuotes='\'');
    std::tuple<CommandPart, CommandPart> splitFirstEntering(char c);

    static CommandPart join(std::vector<CommandPart>& parts, char separator=' ');

    friend bool operator==(CommandPart part, std::string string);
};


#endif //MYSHELL_COMMANDPART_H
