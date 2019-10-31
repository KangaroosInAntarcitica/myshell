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

#include "wildcards.h"
#include "CommandPart.h"

char** convertToCArgs(const std::vector<std::string>& variables) {
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
}

char** convertToCVariables(const std::unordered_map<std::string, std::string>& variables) {
    std::vector<std::string> variableStrings{variables.size()};
    size_t i = 0;
    for (auto &element: variables) {
        variableStrings[i++] = element.first + "=" + element.second;
    }

    return convertToCArgs(variableStrings);
}

class MyShell {
    using variables_t = std::unordered_map<std::string, std::string>;

    variables_t variables;
    variables_t envVariables;
    std::string workingDir;
    int errorno = 0;

public:
    MyShell(std::string path="") {
        workingDir = boost::filesystem::current_path().string() + "/";

        for (size_t i = 0; environ[i] != nullptr; ++i) {
            assignVariable(environ[i], envVariables);
        }

        std::string binDir = workingDir;
        if (!path.empty()) {
            boost::filesystem::path tryBinPath = boost::filesystem::path(path).parent_path().string();
            if (boost::filesystem::is_directory(tryBinPath)) binDir = tryBinPath.lexically_normal().string();
            tryBinPath = boost::filesystem::path(workingDir + "/" + tryBinPath.string());
            if (boost::filesystem::is_directory(tryBinPath)) binDir = tryBinPath.lexically_normal().string();
        }
        envVariables["PATH"] = envVariables["PATH"] + ":" + binDir;
    };

    void run() {
        char *s;

        std::string printString = workingDir + " > ";
        while ((s = readline(printString.c_str())) != nullptr) {
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

    void expandAllCommands(CommandPart part, std::vector<CommandPart>& result) {
        std::vector<CommandPart> parts = part.splitCommand();
        for (auto& part: parts) expandCommandPart(part, result);
    }

    void expandCommandPart(CommandPart& part, std::vector<CommandPart>& result) {
        if (part.quotes == '"') {
            std::vector<CommandPart> parts = part.splitEntering(' ');
            std::vector<CommandPart> subResult;
            for (auto subPart: parts) expandCommandPart(subPart, subResult);
            result.push_back(CommandPart::join(subResult, ' '));
        }
        else if (part.includesEntering('=')) {
            CommandPart first, second;
            std::tie(first, second) = part.splitFirstEntering('=');
            second.quotes = '\"';
            if (!first.empty()) result.push_back(first);
            result.emplace_back("=");
            if (!second.empty()) expandCommandPart(second, result);
        }
        else if (part.string[0] == '$' && !part.escaped[0]) {
            std::string key = part.string.substr(1);
            std::string value;
            value = envVariables[key];
            if (value.empty()) value = variables[key];
            if (!value.empty()){
                CommandPart subPart{value};
                expandAllCommands(subPart, result);
            }
        }
        else if (isWildCard(part)) {
            std::vector<std::string> expandedPart = expandWildCard(part);
            result.insert(result.end(), expandedPart.begin(), expandedPart.end());
        }
        else {
            result.push_back(part);
        }
    }

    static void printHelp(const CommandPart command) {
        if (command == "mexport") std::cout << "mexport <var_name>[=VAL]\nStores the value as global variable";
        else if (command == "merrno") std::cout << "merrno [-h|--help] – display the end code of the last program or command";
        else if (command == "mpwd") std::cout << "mpwd [-h|--help] – display current path";
        else if (command == "mcd") std::cout << "mcd <path> [-h|--help]  - change path to <path>";
        else if (command == "mexit") std::cout << "mexit [exit code] [-h|--help]  – exit from myshell with [exit code]";
        else if (command == "mecho") std::cout << "mecho [text|$<var_name>] [text|$<var_name>]  [text|$<var_name>] - print arguments";

        std::cout << std::endl;
    };

    static bool isHelpPrint(std::vector<CommandPart> lineParts) {
        for (auto &part: lineParts) {
            if (part == "-h" || part == "--help") {
                printHelp(lineParts[0]);
                return true;
            }
        }
        return false;
    };

    void assignVariable(std::string str, variables_t& variables) {
        size_t equalPos;
        if ((equalPos = str.find('=')) != std::string::npos) {
            if (equalPos == 0)
                throw std::invalid_argument("No variable name given");
            std::string key = str.substr(0, equalPos);
            std::string value = str.substr(equalPos + 1);

            variables[key] = value;
        }
        else {
            variables[str] = "1";
        }
    }

    void assignVariable(std::vector<CommandPart>& lineParts, variables_t& variables) {
        if (lineParts.size() == 1) {
            variables[lineParts[0].string] = "1";
        } else if (lineParts.size() == 2) {
            variables[lineParts[0].string] = "";
        } else {
            variables[lineParts[0].string] = lineParts[2].string;
        }
    }

    void execute(const CommandPart path, std::vector<CommandPart>& arguments) {
        pid_t pid = fork();
        if (pid == -1) {
            throw std::runtime_error("Could not start new process");
        }
        else if (pid > 0) {
            waitpid(pid, &errorno, 0);
            errorno = errorno >> 8;
        }
        else {
            std::vector<std::string> args(arguments.size());
            for (size_t i = 0; i < arguments.size(); ++i) args[i] = arguments[i].string;
            char** argumentsString = convertToCArgs(args);
            char** variablesString = convertToCVariables(envVariables);
            execve(path.string.c_str(), (char **) argumentsString, (char **) variablesString);
        }
    }

    void executeSingleCommand(std::string line) {
        std::vector<CommandPart> lineParts;
        expandAllCommands(CommandPart(std::move(line)), lineParts);
        if (lineParts.empty()) return;

        // BUILT-IN COMMANDS
        CommandPart command = lineParts[0];
        if (command == "mexport") {
            if (isHelpPrint(lineParts)) return;
            else if (lineParts.size() < 2 || lineParts.size() > 4)
                throw std::invalid_argument("Invalid number of arguments");
            lineParts.erase(lineParts.begin());
            assignVariable(lineParts, envVariables);
        }
        else if (lineParts.size() > 1 && lineParts[1] == "=") {
            if (lineParts.size() > 3)
                throw std::invalid_argument("Invalid number of arguments");
            assignVariable(lineParts, variables);
        }
        else if (command == "merrno") {
            if (isHelpPrint(lineParts)) return;
            if (lineParts.size() > 1) throw std::invalid_argument("Invalid number of arguments");
            std::cout << errorno << std::endl;
        }
        else if (command == "mpwd") {
            if (isHelpPrint(lineParts)) return;
            if (lineParts.size() > 1) throw std::invalid_argument("Invalid number of arguments");
            std::cout << workingDir << std::endl;
        }
        else if (command == "mcd") {
            if (isHelpPrint(lineParts)) return;
            if (lineParts.size() > 2) throw std::invalid_argument("Invalid number of arguments");
            std::string dirPart = lineParts[1].string;
            std::string newWorkingDir;
            if (dirPart[0] == '/') newWorkingDir = dirPart;
            else newWorkingDir = workingDir + "/" + dirPart;

            boost::filesystem::path dir{newWorkingDir};
            if (!boost::filesystem::is_directory(dir)) {
                throw std::invalid_argument("Path not a directory");
            }
            workingDir = dir.lexically_normal().string();
            if (workingDir[workingDir.length() - 1] == '.')
                workingDir = workingDir.substr(0, workingDir.length() - 1);
            if (workingDir[workingDir.length() - 1] != '/')
                workingDir += '/';
        }
        else if (command == "mexit") {
            if (isHelpPrint(lineParts)) return;
            if (lineParts.size() > 2) throw std::invalid_argument("Invalid number of arguments");
            int exitCode = 0;
            if (lineParts.size() == 2) {
                try {
                    exitCode = std::stoi(lineParts[1].string);
                } catch(...) {
                    throw std::invalid_argument("Invalid argument provided");
                }
            }

            _exit(exitCode);
        }
        else if (command == "mecho") {
            for (size_t i = 1; i < lineParts.size(); ++i) {
                std::cout << lineParts[i].string << (i != lineParts.size() - 1 ? " " : "");
            }
            std::cout << std::endl;
        }

        // CURRENT DIRECTORY COMMANDS
        else if (command.subPart(0, 2) == "./") {
            boost::filesystem::path path = boost::filesystem::path{workingDir + "/" + command.string};
            if (!boost::filesystem::is_regular_file(path))
                throw std::invalid_argument("File not found: " + command.string);
            std::string pathString = path.lexically_normal().string();
            execute(pathString, lineParts);
        }

        // PATH COMMANDS
        else {
            std::string fullCommand;

            size_t directoryI = 0;
            size_t prevDirectoryI = 0;
            std::string path = envVariables["PATH"];

            while (directoryI != path.length()) {
                directoryI = path.find(':', prevDirectoryI);
                if (directoryI == std::string::npos) directoryI = path.length();

                std::string executable = path.substr(prevDirectoryI, directoryI - prevDirectoryI) + "/" + command.string;

                if (!executable.empty() && boost::filesystem::is_regular_file(executable)) {
                    fullCommand = boost::filesystem::path{executable}.lexically_normal().string();
                }

                prevDirectoryI = directoryI + 1;
            }

            if (fullCommand.empty()) {
                throw std::invalid_argument("Command not found: " + command.string);
            }
            execute(fullCommand, lineParts);
        }
    }
};


int main(int argc, char** argv) {
    MyShell shell{argc > 0 ? argv[0] : ""};
    shell.run();

    return 0;
}