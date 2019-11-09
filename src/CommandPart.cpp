#include "CommandPart.h"

#include <iostream>


CommandPart::CommandPart(const std::string string, bool escape, char escapeChar)
{
    if (!escape) {
        this->string = string;
        this->escaped = std::vector<bool>(string.length());
        return;
    }

    std::stringstream resultString;
    std::vector<bool> resultEscaped(string.length());
    size_t index = 0;
    for (size_t i = 0; i < string.length(); ++i) {
        if (i != string.length() - 1 && string[i] == escapeChar) {
            resultString << string[++i];
            resultEscaped[index] = true;
        } else {
            resultString << string[i];
        }
        index++;
    }

    resultEscaped.erase(resultEscaped.begin() + index, resultEscaped.end());
    this->string = resultString.str();
    this->escaped = resultEscaped;
}
size_t CommandPart::size() { return string.length(); }
bool CommandPart::empty() { return string.empty(); }
char& CommandPart::operator[](size_t i) {
    return string[i];
};

// entering - not escaped
bool CommandPart::includesEntering(char c) {
    return findEntering(c) != std::string::npos;
}

size_t CommandPart::findEntering(char c) {
    for (size_t i = 0; i < string.length(); ++i) {
        if (string[i] == c && !escaped[i]) return i;
    }
    return std::string::npos;
}

CommandPart CommandPart::subPart(size_t start, size_t end) {
    start = start > string.length() ? string.length() : start;
    end = end > string.length() ? string.length() : end;
    size_t length = end - start;

    CommandPart result{};
    result.string = string.substr(start, length);
    result.escaped = std::vector<bool>(length);
    for (size_t i = 0; i < length; ++i) {
        result.escaped[i] = escaped[start + i];
    }
    return result;
}

std::vector<CommandPart> CommandPart::splitEntering(char c) {
    std::vector<CommandPart> result;
    size_t startI = 0;
    for (size_t i = 0; i <= string.length(); ++i) {
        if (i == string.length() || (string[i] == c && !escaped[i])) {
            if (i - startI > 0) result.push_back(subPart(startI, i));
            startI = i + 1;
        }
    }
    return result;
}

std::vector<CommandPart> CommandPart::splitCommand(char separator) {
    std::vector<CommandPart> result;
    size_t startI = 0;
    char quotes = 0;

    for (size_t i = 0; i <= string.length(); ++i) {
        if (i != string.length() && escaped[i]) continue;
        if (i == string.length() || (!quotes && string[i] == separator)) {
            if (i - startI > 0) result.push_back(subPart(startI, i));
            startI = i + 1;
        } else if (string[i] == '"' || string[i] == '\'' ||
            (string[i] == '$' && i < string.length() && string[i+1] == '(') ||
            (quotes == '$' && string[i] == ')'))
        {
            if (quotes == string[i] || (quotes == '$' && string[i] == ')')) {
                CommandPart sub = subPart(startI, i);
                sub.quotes = quotes;
                if (string[i] == '\'') {
                    for (size_t i = 0; i < sub.escaped.size(); ++i) sub.escaped[i] = true;
                }
                result.push_back(sub);
                quotes = 0;
                startI = i + 1;
            } else if (quotes == 0) {
                if (i - startI > 0) result.push_back(subPart(startI, i));
                quotes = string[i];
                startI = i + (quotes == '$' ? 2 : 1);
            }
        }
    }

    return result;
}

std::tuple<CommandPart, CommandPart> CommandPart::splitFirstEntering(char c) {
    size_t i = findEntering(c);
    return std::make_tuple(subPart(0, i), subPart(i + 1));
}

CommandPart CommandPart::join(std::vector<CommandPart>& parts, char separator) {
    std::ostringstream resultString;

    for (size_t i = 0; i < parts.size(); ++i) {
        resultString << parts[i].string;
        if (i != parts.size() - 1) resultString << separator;
    }

    CommandPart result(resultString.str());
    size_t i = 0;
    for (auto& part: parts) {
        for (size_t j = 0; j < part.size(); ++j, ++i) {
            result.escaped[i + j] = part.escaped[j];
        }
        ++i;
    }
    return result;
}

bool operator==(CommandPart part, std::string string) {
    return part.string == string;
}
