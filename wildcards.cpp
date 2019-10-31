#include "wildcards.h"

#include <sstream>
#include <stdexcept>
#include <boost/filesystem.hpp>

bool testWildCard(std::string str, CommandPart& wildCard, size_t strI, size_t wildCardI) {
    for (;strI < str.size() && wildCardI < wildCard.size(); ++strI, ++wildCardI) {
        if (wildCard.escaped[wildCardI]) {
            if (str[strI] != wildCard[++wildCardI]) return false;
        } else if (wildCard[wildCardI] == '[') {
            bool passed = false;
            ++wildCardI;

            for (;!wildCard.escaped[wildCardI] && wildCard[wildCardI] != ']'; ++wildCardI) {
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

bool testWildCard(std::string str, CommandPart wildCard) {
    return testWildCard(str, wildCard, 0, 0);
}

bool isWildCard(CommandPart& part) {
    for (size_t i = 0; i < part.size(); ++i) {
        if (part.escaped[i]) continue;
        if (part[i] == '[' || part[i] == '*' || part[i] == '?') return true;
    }
    return false;
}

std::vector<std::string> expandWildCard (CommandPart partToParse) {
    size_t filenameSlash = partToParse.string.find_last_of('/');
    std::string directory = filenameSlash == std::string::npos ? "." : partToParse.string.substr(0, filenameSlash);
    CommandPart wildCard = filenameSlash == std::string::npos ? partToParse : partToParse.subPart(filenameSlash + 1);

//    if (isWildCard(directory))
//        throw std::runtime_error("Wild card is not supported for directories (" + directory + ")");
    std::vector<std::string> passedFiles;

    boost::filesystem::directory_iterator endIterator;
    for(boost::filesystem::directory_iterator iterator(directory); iterator != endIterator; ++iterator) {
        if (!boost::filesystem::is_regular_file(iterator->status())) continue;

        boost::filesystem::path file = *iterator;
        if (testWildCard(file.filename().string(), wildCard))
            passedFiles.push_back(file.lexically_normal().string());
    }

    if (passedFiles.empty()) {
        throw std::runtime_error("Wild card " + partToParse.string + " could not be extended.");
    }
    return passedFiles;
}
