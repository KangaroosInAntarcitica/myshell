#include <iostream>
#include <unordered_map>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>
#include <sys/wait.h>

#include <boost/filesystem.hpp>
#include <readline/readline.h>
#include <readline/history.h>

extern char** environ;

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

char** convertToCArgs(std::vector<std::string> variables) {
    char** result = new char*[variables.size() + 1];

    for (size_t i = 0; i < variables.size(); ++i) {
        result[i] = new char[variables[i].length() + 1];
        for (size_t c = 0; c < variables[i].length(); ++c) {
            result[i][c] = variables[i][c];
        }
        result[i][variables[i].length()] = '\0';
    }
    result[variables.size()] = nullptr;

    return result;
};

char** convertToCVariables(std::unordered_map<std::string, std::string> variables) {
    std::vector<std::string> variableStrings{variables.size()};
    size_t i = 0;
    for (auto &element: variables) {
        variableStrings[i++] = element.first + "=" + element.second;
    }

    return convertToCArgs(variableStrings);
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
    variables_t envVariables;
    std::string workingDir;
    int errorno = 0;

public:
    MyShell() {
        workingDir = boost::filesystem::current_path().string() + "/";

        for (size_t i = 0; environ[i] != nullptr; ++i) {
            assignVariable(environ[i], envVariables);
        }
        envVariables["PATH"] = envVariables["PATH"] + ":" + workingDir;
    };

    void run() {
        char *s;

        std::string printString = workingDir + " > ";
        while ((s = readline(printString.c_str())) != NULL) {
            add_history(s);

            try {
                executeSingleCommand(s);
            } catch(std::exception &e) {
                std::cout << e.what() << std::endl;
            }

            free(s);
            printString = workingDir + " > ";
        }
    }

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
                    if (quotes != '\'' && partString[0] == '$') {
                        std::string key = partString.substr(1);
                        std::string value;
                        value = envVariables[key];
                        if (value.empty()) value = variables[key];
                        if (!value.empty()) lineParts.push_back(value);
                    }
                    else if (quotes != '\'' && isWildCard(partString)) {
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

    void printHelp(std::string command) {
        if (command == "mexport") std::cout << "mexport <var_name>[=VAL]\nStores the value as global variable";
        else if (command == "merrno") std::cout << "merrno [-h|--help] – display the end code of the last program or command";
        else if (command == "mpwd") std::cout << "mpwd [-h|--help] – display current path";
        else if (command == "mcd") std::cout << "mcd <path> [-h|--help]  - change path to <path>";
        else if (command == "mexit") std::cout << "mexit [exit code] [-h|--help]  – exit from myshell with [exit code]";
        else if (command == "mecho") std::cout << "mecho [text|$<var_name>] [text|$<var_name>]  [text|$<var_name>] - print arguments";

        std::cout << std::endl;
    };

    bool isHelpPrint(std::vector<std::string> lineParts) {
        for (auto part: lineParts) {
            if (part == "-h" || part == "--help") {
                printHelp(lineParts[0]);
                return true;
            }
        }
        return false;
    };

    void assignVariable(std::string command, variables_t& variables) {
        size_t equalPos;
        if ((equalPos = command.find('=')) != std::string::npos) {
            if (equalPos == 0)
                throw std::runtime_error("No variable name given");
            std::string key = command.substr(0, equalPos);
            std::string value = command.substr(equalPos + 1);

            if (value[0] == '$') {
                std::string replaceKey = value.substr(1);
                value = envVariables[replaceKey];
                if (value.empty()) value = variables[replaceKey];
            }
            variables[key] = value;
        }
        else {
            throw std::runtime_error("Assignment should include equals sign");
        }
    }

    void execute(std::string path, std::vector<std::string> arguments) {
        pid_t pid = fork();
        if (pid == -1) {
            throw std::runtime_error("Could not start new process");
        }
        else if (pid > 0) {
            waitpid(pid, &errorno, 0);
            errorno >>= 8;
        }
        else {
            char** argumentsString = convertToCArgs(arguments);
            char** variablesString = convertToCVariables(envVariables);
            execve(path.c_str(), (char **) argumentsString, (char **) variablesString);
        }
    }

    void executeSingleCommand(std::string line) {
        std::vector<std::string> lineParts = splitCommandIntoParts(line);
        if (lineParts.empty()) return;

        std::string command = lineParts[0];
        if (command == "mexport") {
            if (isHelpPrint(lineParts)) return;
            else if (lineParts.size() == 1 || lineParts.size() > 2)
                throw std::runtime_error("Invalid number of arguments");
            assignVariable(lineParts[1], envVariables);
        }
        else if (command.find('=') != std::string::npos) {
            if (isHelpPrint(lineParts)) return;
            if (lineParts.size() > 1) throw std::runtime_error("Invalid number of arguments");
            assignVariable(lineParts[0], variables);
        }
        else if (command == "merrno") {
            if (isHelpPrint(lineParts)) return;
            if (lineParts.size() > 1) throw std::runtime_error("Invalid number of arguments");
            std::cout << errorno << std::endl;
        }
        else if (command == "mpwd") {
            if (isHelpPrint(lineParts)) return;
            if (lineParts.size() > 1) throw std::runtime_error("Invalid number of arguments");
            std::cout << workingDir << std::endl;
        }
        else if (command == "mcd") {
            if (isHelpPrint(lineParts)) return;
            if (lineParts.size() > 2) throw std::runtime_error("Invalid number of arguments");
            std::string dirPart = lineParts[1];
            std::string newWorkingDir;
            if (dirPart[0] == '/') newWorkingDir = dirPart;
            else newWorkingDir = workingDir + "/" + dirPart;

            boost::filesystem::path dir{newWorkingDir};
            if (!boost::filesystem::is_directory(dir)) {
                throw std::runtime_error("Path not a directory");
            }
            workingDir = dir.lexically_normal().string();
            if (workingDir[workingDir.length() - 1] == '.')
                workingDir = workingDir.substr(0, workingDir.length() - 1);
            if (workingDir[workingDir.length() - 1] != '/')
                workingDir += '/';
        }
        else if (command == "mexit") {
            if (isHelpPrint(lineParts)) return;
            if (lineParts.size() > 2) throw std::runtime_error("Invalid number of arguments");
            int exitCode = 0;
            if (lineParts.size() == 2) {
                try {
                    exitCode = std::stoi(lineParts[1]);
                } catch(...) {
                    throw std::runtime_error("Invalid argument provided");
                }
            }

            _exit(exitCode);
        }
        else if (command == "mecho") {
            for (size_t i = 1; i < lineParts.size(); ++i) {
                std::cout << lineParts[i] << (i != lineParts.size() - 1 ? " " : "");
            }
            std::cout << std::endl;
        }


        else if (command.substr(0, 2) == "./") {
            boost::filesystem::path path = boost::filesystem::path{workingDir + "/" + command};
            if (!boost::filesystem::is_regular_file(path))
                throw std::runtime_error("File not found: " + command);
            std::string pathString = path.lexically_normal().string();
            execute(pathString, lineParts);
        }
        else {
            std::string fullCommand;

            size_t directoryI = 0;
            size_t prevDirectoryI = 0;
            std::string path = envVariables["PATH"];

            while (directoryI != path.length()) {
                directoryI = path.find(':', prevDirectoryI);
                if (directoryI == std::string::npos) directoryI = path.length();

                std::string executable = path.substr(prevDirectoryI, directoryI - prevDirectoryI) + "/" + command;

                if (!executable.empty() && boost::filesystem::is_regular_file(executable)) {
                    fullCommand = boost::filesystem::path{executable}.lexically_normal().string();
                }

                prevDirectoryI = directoryI + 1;
            }

            if (fullCommand.empty()) {
                throw std::runtime_error("Command not found");
            }
            execute(fullCommand, lineParts);
        }
    }
};


int main(int argc, char** argv) {
    std::vector<std::string> res = expandWildCard("*");

    MyShell shell{};
    shell.run();

    return 0;
}