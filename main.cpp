#include <iostream>
#include <unordered_map>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

#include <boost/filesystem.hpp>


bool testWildCard(std::string& str, std::string& wildCard, size_t strI, size_t wildCardI) {
    for (;strI < str.size() && wildCardI < wildCard.size(); ++strI, ++wildCardI) {
        if (wildCard[wildCardI] == '\\') {
            if (wildCardI == wildCard.size() - 1)
                throw std::invalid_argument(R"(Invalid "\" in the end of parameter)");
            if (str[strI] != wildCard[++wildCardI]) return false;
        } else if (wildCard[wildCardI] == '[') {
            bool passed = false;
            ++wildCardI;

            for (;wildCard[wildCardI] != ']'; ++wildCardI) {
                if (wildCardI == wildCard.size() - 1)
                    throw std::invalid_argument(R"(Wild card starting with "[" has no closing bracket)");

                if (wildCard[wildCardI] == '\\') ++wildCardI;
                if (wildCard[wildCardI] == str[strI]) {
                    passed = true;
                    break;
                }
            }

            ++wildCardI; // skip ]
            if (!passed) return false;
        } else if (wildCard[wildCardI] == '*') {
            for (;strI <= str.size(); ++strI) {
                if (testWildCard(str, wildCard, strI, wildCardI + 1)) return true;
            }

            return false;
        } else if (wildCard[wildCardI] != '?') {
            if (str[strI] != wildCard[wildCardI]) return false;
        }
    }

    return strI == str.size() && wildCardI == wildCard.size();
}

bool testWildCard(std::string str, std::string wildCard) {
    return testWildCard(str, wildCard, 0, 0);
}

bool isWildCard(std::string part) {
    for (size_t i = 0; i < part.size(); ++i) {
        if (part[i] == '[' || part[i] == '*' || part[i] == '?') return true;
        if (part[i] == '\\') ++i;
    }
    return false;
}

std::vector<std::string> expandWildCard (std::string partToParse) {
    size_t filenameSlash = partToParse.find_last_of('/');
    std::string directory = filenameSlash == std::string::npos ? "." : partToParse.substr(0, filenameSlash);
    std::string wildCard = filenameSlash == std::string::npos ? partToParse : partToParse.substr(filenameSlash + 1);

    if (isWildCard(directory))
        throw std::runtime_error("Wild card is not supported for directories (" + directory + ")");

    std::vector<std::string> passedFiles;

    boost::filesystem::directory_iterator endIterator;
    for(boost::filesystem::directory_iterator iterator(directory); iterator != endIterator; ++iterator) {
        if (!boost::filesystem::is_regular_file(iterator->status())) continue;

        boost::filesystem::path file = *iterator;
        if (testWildCard(file.filename().string(), wildCard))
            passedFiles.push_back(file.string());
    }

    if (passedFiles.empty()) {
        throw std::runtime_error("Wild card " + partToParse + " could not be extended.");
    }
    return passedFiles;
};

class MyShell {
    using variables_t = std::unordered_map<std::string, std::string>;

    variables_t variables;
    variables_t env_variables;
    boost::filesystem::path working_dir;
    int errno;

    bool doesDirectoryExist();
    bool doesFileExist();
    std::vector<std::string> listDirectory();

public:
    MyShell() {
        working_dir = boost::filesystem::current_path();
        env_variables["PATH"] = working_dir.string() + ":";
    };

    std::vector<std::string> splitCommandIntoParts(std::string line) {
        line = line + ' ';
        std::vector<std::string> lineParts;

        // read the parts
        std::ostringstream part;
        bool partBegin = true;
        bool partEnd = false;
        char quotes = 0;
        char prevC = 0;

        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];

            if (c == '\\' && i + 1 < line.size() && (line[i+1] == '\"' || line[i + 1] == '\'')) {
                part << line[++i];
            } else if (c == '\"' || c == '\'') {
                if (partBegin) {
                    quotes = c;
                } else if (quotes != c) {
                    part << line[i];
                } else {
                    // TODO throw exception if no space
                    partEnd = true;
                }
            } else if (c == ' ' && !quotes) {
                partEnd = true;
            } else {
                part << line[i];
            }

            if (partEnd) {
                std::string partString = part.str();
                part = std::ostringstream();

                if (!partString.empty()) {
                    if (quotes != '\'' && isWildCard(partString)) {
                        std::vector<std::string> expandedPart = expandWildCard(partString);
                        lineParts.insert(lineParts.end(), expandedPart.begin(), expandedPart.end());
                    }
                    else lineParts.push_back(partString);
                }
                partEnd = false;
                partBegin = true;
                quotes = 0;
            } else {
                partBegin = false;
            }
        }

        return lineParts;
    };

    void executeSingleCommand(std::string line) {
        std::cout << line << std::endl;
        std::vector<std::string> lineParts = splitCommandIntoParts(line);

        for (auto part: lineParts) std::cout << part << std::endl;
    }
};


int main(int argc, char** argv) {
    std::vector<std::string> res = expandWildCard("*");

    MyShell shell{};
    std::string input;
    std::getline(std::cin, input);
    shell.executeSingleCommand(input);

    return 0;
}