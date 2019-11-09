#include <iostream>
#include <map>
#include <unordered_map>
#include <string>
#include <vector>

#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <fstream>

#include <boost/filesystem.hpp>
#include <readline/readline.h>
#include <readline/history.h>

#include "wildcards.h"
#include "CommandPart.h"
#include "redirectsParser.h"
#include "system_read_write.h"

struct ignore_error: std::exception {};

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

template <typename ...Args>
int callSystem(std::string errorString, int(* sysCall)(Args...), Args... args) {
    int result;
    while(true) {
        errno = 0;
        if ((result = sysCall(args...)) < 0) {
            if (errno != EINTR) {
                throw std::runtime_error(errorString);
            }
        } else break;
    }
    return result;
}

struct Redirecting {
    std::map<int, int> redirects;
    std::vector<int> filesToClose;
    std::vector<int> parentFilesToClose;
    std::map<int, int> backRedirects;

    // should be false if there is nothing receiving on the other end
    bool redirectIfBuiltIn = true;
    std::string builtInStdOut;
    std::string builtInStdErr;
    bool isBuiltIn = false;

    int childPid;
    bool wait = true;

    int get(int from) {
        return redirects.count(from) ? redirects[from] : from;
    };
    void addFileToClose(int file) {
        filesToClose.push_back(file);
    }
    void addParentFileToClose(int file) {
        parentFilesToClose.push_back(file);
    }
    int set(int from, int to) {
        redirects[from] = to;
    }
    void changeDescriptor(int from, int to, bool saveBackwardRedirects=false) {
        if (from == to) return;
        if (saveBackwardRedirects) {
            backRedirects[from] = callSystem("Could not duplicate file descriptor.", dup, from);
        }
        callSystem("Could not replace file descriptor.", dup2, to, from);
    }
    void apply(bool saveBackwardRedirects=false) {
        for (auto& redirect: redirects) {
            changeDescriptor(redirect.first, redirect.second, saveBackwardRedirects);
        }
        redirects.clear();

    }
    void closeChild() {
        for (auto& fileToClose: filesToClose) {
            close(fileToClose);
        }
        filesToClose.clear();
    }
    void closeParent() {
        for (auto& file: parentFilesToClose) {
            close(file);
        }
        parentFilesToClose.clear();
    }

    void revert() {
        for (auto& redirect: backRedirects) {
            changeDescriptor(redirect.first, redirect.second, false);
            callSystem("Failed to close file descriptor", close, redirect.second);
        }
        backRedirects.clear();
    }
    bool empty() const { return redirects.empty(); }
    void merge(Redirecting& o) {
        for (auto& redirect: o.redirects) redirects[redirect.first] = redirect.second;
        filesToClose.insert(filesToClose.begin(), o.filesToClose.begin(), o.filesToClose.end());
        parentFilesToClose.insert(parentFilesToClose.begin(), o.parentFilesToClose.begin(), o.parentFilesToClose.end());
        for (auto& redirect: o.backRedirects) backRedirects[redirect.first] = redirect.second;
    }
};

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
                executeSingleLine(CommandPart{std::string(s)});
            } catch(std::exception &e) {
                writeAll(STDERR_FILENO, e.what() + std::string("\n"));
            }

            free(s);
            printString = workingDir + " > ";
        }
    }
    void run(std::string script) {
        if (!boost::filesystem::is_regular_file(script)) {
           writeAll(STDERR_FILENO, "Could not find file: " + script + "\n");
           return;
        }
        std::ifstream infile(script);

        std::string s;
        while (std::getline(infile, s)) {
            try {
                executeSingleLine(CommandPart{s});
            } catch(std::exception &e) {
                writeAll(STDERR_FILENO, e.what() + std::string("\n"));
            }
        }
    }

    void expandSingleLine(CommandPart part, std::vector<CommandPart>& result) {
        std::vector<CommandPart> parts = part.splitCommand();

        for (auto& part: parts) {
            // Deal with comments
            if (!part.quotes && part.includesEntering('#')) {
                // Expand the part before comment and stop
                CommandPart before, after;
                std::tie(before, after) = part.splitFirstEntering('#');
                if (!before.empty()) expandCommandPart(before, result);
                break;
            }
            expandCommandPart(part, result);
        }
    }

    void expandCommandPart(CommandPart& part, std::vector<CommandPart>& result, bool expandVariables=true, bool expandWildCards=true) {
        if (part.quotes == '"') {
            std::vector<CommandPart> parts = part.splitEntering(' ');
            std::vector<CommandPart> subResult;
            // expand each of parts separately
            for (auto subPart: parts) expandCommandPart(subPart, subResult, expandVariables, false);
            result.push_back(CommandPart::join(subResult, ' '));
        }
        else if (expandVariables && part.quotes == '$') {
            int pipefd[2];
            callSystem("Error creating pipe.", pipe, pipefd);
            Redirecting redirecting;
            redirecting.redirectIfBuiltIn = false;
            redirecting.set(STDOUT_FILENO, pipefd[1]);
            redirecting.addFileToClose(pipefd[0]);
            redirecting.addParentFileToClose(pipefd[1]);

            executeSingleLine(part, redirecting);
            std::string value;
            if (redirecting.isBuiltIn) {
                value = redirecting.builtInStdOut;
            } else {
                value = readAll(pipefd[0]);
            }

            if (!value.empty()) result.push_back(value);
        }
        else if (part.includesEntering('=')) {
            CommandPart first, second;
            std::tie(first, second) = part.splitFirstEntering('=');
            if (!first.empty()) result.push_back(first);
            result.emplace_back("=");
            std::vector<CommandPart> subResult;
            // expand second part and join to a single string
            if (!second.empty()) expandCommandPart(second, subResult);
            if (!subResult.empty()) result.push_back(CommandPart::join(subResult, ' '));
        }
        else if (expandVariables && part.string[0] == '$' && !part.escaped[0]) {
            std::string key = part.string.substr(1);
            std::string value;
            value = envVariables[key];
            if (value.empty()) value = variables[key];
            if (!value.empty()) {
                result.push_back(value);
                CommandPart valuePart{value, false};
                // expand the value
                std::vector<CommandPart> subParts = valuePart.splitCommand();
                for (auto& subPart: subParts) expandCommandPart(subPart, result, false, expandWildCards);
            }
        }
        else if (expandWildCards && isWildCard(part)) {
            // expand each wildCard
            std::vector<std::string> expandedPart = expandWildCard(part);
            result.insert(result.end(), expandedPart.begin(), expandedPart.end());
        }
        else {
            result.push_back(part);
        }
    }

    static void printHelp(const CommandPart command, Redirecting& redirecting) {
        if (command == "mexport") redirecting.builtInStdOut = "mexport <var_name>[=VAL]\nStores the value as global variable\n";
        else if (command == "merrno") redirecting.builtInStdOut = "merrno [-h|--help] – display the end code of the last program or command\n";
        else if (command == "mpwd") redirecting.builtInStdOut = "mpwd [-h|--help] – display current path\n";
        else if (command == "mcd") redirecting.builtInStdOut = "mcd <path> [-h|--help]  - change path to <path>\n";
        else if (command == "mexit") redirecting.builtInStdOut = "mexit [exit code] [-h|--help]  – exit from myshell with [exit code]\n";
        else if (command == "mecho") redirecting.builtInStdOut = "mecho [text|$<var_name>] [text|$<var_name>]  [text|$<var_name>] - print arguments\n";
        else if (command == ".") redirecting.builtInStdOut = ". [script] Execute the given script\n";
    };

    static bool isHelpPrint(std::vector<CommandPart> lineParts, Redirecting& redirecting) {
        for (auto &part: lineParts) {
            if (part == "-h" || part == "--help") {
                printHelp(lineParts[0], redirecting);
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

    void execute(const CommandPart path, std::vector<CommandPart>& arguments, Redirecting& redirecting, bool wait=true) {
        pid_t pid = fork();
        if (pid == -1) {
            throw std::runtime_error("Could not start new process");
        }
        else if (pid > 0) {
            // close all the required files
            redirecting.closeParent();
            redirecting.childPid = pid;
            redirecting.wait = wait;
            errorno = errorno >> 8;
        }
        else {
            std::vector<std::string> args(arguments.size());
            for (size_t i = 0; i < arguments.size(); ++i) args[i] = arguments[i].string;
            char** argumentsString = convertToCArgs(args);
            char** variablesString = convertToCVariables(envVariables);

            if (!wait) {
                close(STDOUT_FILENO); close(STDERR_FILENO); close(STDIN_FILENO);
            }
            redirecting.apply();
            redirecting.closeChild();
            execve(path.string.c_str(), (char **) argumentsString, (char **) variablesString);

            exit(1);
        }
    }
    void executeShellScript(CommandPart script, Redirecting& redirecting) {
        pid_t pid = fork();
        if (pid == -1) {
            throw std::runtime_error("Could not start new process");
        }
        else if (pid > 0) {
            // close all the required files
            redirecting.closeParent();
            redirecting.childPid = pid;
            redirecting.wait = wait;
            errorno = errorno >> 8;
        }
        else {
            if (!wait) {
                close(STDOUT_FILENO); close(STDERR_FILENO); close(STDIN_FILENO);
            }
            redirecting.apply();
            redirecting.closeChild();
            run(script.string);
            exit(1);
        }
    }

    void executeSingleLine(CommandPart line) { Redirecting redirecting{}; executeSingleLine(line, redirecting); }
    void executeSingleLine(CommandPart line, Redirecting& finalRedirecting) {
        // split line into parts, while expanding all the wildcards and variables
        std::vector<CommandPart> lineParts;
        expandSingleLine(CommandPart(std::move(line)), lineParts);

        if (lineParts.empty()) return;

        // Deal with all the redirects and pipes
        std::vector<CommandPart> currentCommandParts;
        Redirecting currentCommandRedirecting;
        std::vector<int> openFiles{};
        std::vector<Redirecting> allRedirectings;

        try {
            for (size_t i = 0; i < lineParts.size(); ++i) {
                CommandPart linePart = lineParts[i];
                // Pipe
                if (linePart == "|" && !linePart.escaped[0]) {
                    if (currentCommandParts.empty()) throw std::invalid_argument("No command supplied to pipe on left");

                    // Create pipe
                    int pipefd[2];
                    callSystem("Error creating pipe.", pipe, pipefd);

                    // Change the redirecting for left command and execute it in the background
                    currentCommandRedirecting.set(STDOUT_FILENO, pipefd[1]);
                    currentCommandRedirecting.addFileToClose(pipefd[0]);
                    currentCommandRedirecting.addParentFileToClose(pipefd[1]);
                    executeSingleCommand(currentCommandParts, currentCommandRedirecting, false);
                    allRedirectings.push_back(currentCommandRedirecting);
                    currentCommandParts = {};

                    // Set the redirecting for next command and continue parsing
                    currentCommandRedirecting = Redirecting{};
                    currentCommandRedirecting.set(STDIN_FILENO, pipefd[0]);
                    currentCommandRedirecting.addParentFileToClose(pipefd[0]);
                    currentCommandRedirecting.addFileToClose(pipefd[1]);
                }
                // Redirect
                else if (isRedirect(linePart)) {
                    if (currentCommandParts.empty())
                        throw std::invalid_argument("No command supplied to redirect on left");

                    int from, direction, to;
                    std::tie(from, direction, to) = parseRedirect(linePart);

                    if (to == -1) {
                        // No target specified
                        if (i == lineParts.size() - 1)
                            throw std::invalid_argument("No redirect target/source specified");
                        if (direction == -1) {
                            int fd = open(lineParts[++i].string.c_str(), O_RDONLY);
                            if (fd < 0) {
                                throw std::runtime_error("Cannot open file " + lineParts[i].string);
                            }
                            currentCommandRedirecting.addParentFileToClose(fd);
                            currentCommandRedirecting.set(from, fd);
                            currentCommandRedirecting.addFileToClose(fd);
                        } else {
                            int fd = open(lineParts[++i].string.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                                          S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                            if (fd < 0) throw std::runtime_error("Cannot open file " + lineParts[i].string);
                            currentCommandRedirecting.addParentFileToClose(fd);
                            currentCommandRedirecting.set(from, fd);
                            currentCommandRedirecting.addFileToClose(fd);
                        }
                    } else {
                        if (direction == -1) currentCommandRedirecting.set(to, from);
                        else currentCommandRedirecting.set(from, to);
                    }
                }
                // In background
                else if (linePart == "&" && !linePart.escaped[0]) {
                    if (currentCommandParts.empty())
                        throw std::invalid_argument("No command supplied to run in background");

                    executeSingleCommand(currentCommandParts, currentCommandRedirecting, false);
                    allRedirectings.push_back(currentCommandRedirecting);
                    currentCommandParts = {};
                    currentCommandRedirecting = Redirecting{};
                } else {
                    currentCommandParts.push_back(linePart);
                }
            }

            // Final command in the end
            if (currentCommandParts.empty()) {
                if (!currentCommandRedirecting.empty()) throw std::invalid_argument("Expected a command.");
            } else {
                finalRedirecting.merge(currentCommandRedirecting);
                executeSingleCommand(currentCommandParts, finalRedirecting, true);
                allRedirectings.push_back(finalRedirecting);
                for (int fileD: openFiles) close(fileD);
            }

            // Perform the built-in commands
            int builtInRedirectings = 0;
            for (auto &redirecting: allRedirectings) {
                if (redirecting.isBuiltIn) ++builtInRedirectings;
            }
            if (builtInRedirectings > 1) throw std::invalid_argument("Only one built-in redirecting allowed!");
            for (auto &redirecting: allRedirectings) {
                // if redirecting should have been executed in this process - perform it now
                if (redirecting.isBuiltIn) {
                    if (redirecting.redirectIfBuiltIn) {
                        // apply redirecting with ability to return back
                        redirecting.apply(true);
                        if (!redirecting.builtInStdOut.empty()) std::cout << redirecting.builtInStdOut;
                        if (!redirecting.builtInStdErr.empty()) writeAll(STDERR_FILENO, redirecting.builtInStdErr);
                        redirecting.revert();
                    }
                    // close all the files that should have been closed
                    redirecting.closeParent();
                }
            }

            // Wait for all other children
            for (auto& redirecting: allRedirectings) {
                if (redirecting.wait) waitpid(redirecting.childPid, &errorno, 0);
            }

        } catch(...) {
            // close all possibly open files
            for (auto& redirecting: allRedirectings) redirecting.closeParent();
            throw;
        }
    }

    void executeSingleCommand(std::vector<CommandPart>& lineParts, Redirecting& redirecting, bool wait=true) {
        // BUILT-IN COMMANDS
        CommandPart command = lineParts[0];
        if (command == ".") {
            if (isHelpPrint(lineParts, redirecting)) {
                redirecting.isBuiltIn = true;
                return;
            }
            else if (lineParts.size() < 2 || lineParts.size() > 2) {
                redirecting.isBuiltIn = true;
                redirecting.builtInStdErr = "Invalid number of arguments";
                return;
            }

            executeShellScript(lineParts[1].string, redirecting);
        }
        else if (command == "mexport") {
            redirecting.isBuiltIn = true;
            if (isHelpPrint(lineParts, redirecting)) return;
            else if (lineParts.size() < 2 || lineParts.size() > 4) {
                redirecting.builtInStdErr = "Invalid number of arguments";
                return;
            }
            lineParts.erase(lineParts.begin());
            assignVariable(lineParts, envVariables);
        }
        else if (lineParts.size() > 1 && lineParts[1] == "=") {
            redirecting.isBuiltIn = true;
            if (lineParts.size() > 3) {
                redirecting.builtInStdErr = "Invalid number of arguments";
                return;
            }
            assignVariable(lineParts, variables);
        }
        else if (command == "merrno") {
            redirecting.isBuiltIn = true;
            if (isHelpPrint(lineParts, redirecting)) return;
            if (lineParts.size() > 1) {
                redirecting.builtInStdErr = "Invalid number of arguments";
                return;
            }
            redirecting.builtInStdOut = std::to_string(errorno) + "\n";
        }
        else if (command == "mpwd") {
            redirecting.isBuiltIn = true;
            if (isHelpPrint(lineParts, redirecting)) return;
            if (lineParts.size() > 1) {
                redirecting.builtInStdErr = "Invalid number of arguments";
                return;
            }
            redirecting.builtInStdOut = workingDir + "\n";
        }
        else if (command == "mcd") {
            redirecting.isBuiltIn = true;
            if (isHelpPrint(lineParts, redirecting)) return;
            if (lineParts.size() > 2) {
                redirecting.builtInStdErr = "Invalid number of arguments";
                return;
            }
            std::string dirPart = lineParts[1].string;
            std::string newWorkingDir;
            if (dirPart[0] == '/') newWorkingDir = dirPart;
            else newWorkingDir = workingDir + "/" + dirPart;

            boost::filesystem::path dir{newWorkingDir};
            if (!boost::filesystem::is_directory(dir)) {
                redirecting.builtInStdErr = "Path not a directory";
                return;
            }
            workingDir = dir.lexically_normal().string();
            if (workingDir[workingDir.length() - 1] == '.')
                workingDir = workingDir.substr(0, workingDir.length() - 1);
            if (workingDir[workingDir.length() - 1] != '/')
                workingDir += '/';
        }
        else if (command == "mexit") {
            redirecting.isBuiltIn = true;
            if (isHelpPrint(lineParts, redirecting)) return;
            if (lineParts.size() > 2) {
                redirecting.builtInStdErr = "Invalid number of arguments";
                return;
            }
            int exitCode = 0;
            if (lineParts.size() == 2) {
                try {
                    exitCode = std::stoi(lineParts[1].string);
                } catch(...) {
                    redirecting.builtInStdErr = "Invalid argument provided";
                    return;
                }
            }

            _exit(exitCode);
        }
        else if (command == "mecho") {
            redirecting.isBuiltIn = true;
            if (isHelpPrint(lineParts, redirecting)) return;
            std::ostringstream stringstream;
            for (size_t i = 1; i < lineParts.size(); ++i) {
                stringstream << lineParts[i].string << (i == lineParts.size() - 1 ? "" : " ");
            }
            stringstream << std::endl;
            redirecting.builtInStdOut = stringstream.str();
        }

        // CURRENT DIRECTORY COMMANDS
        else if (command.subPart(0, 2) == "./") {
            boost::filesystem::path path = boost::filesystem::path{workingDir + "/" + command.string};
            if (!boost::filesystem::is_regular_file(path))
                throw std::invalid_argument("File not found: " + command.string);
            std::string pathString = path.lexically_normal().string();
            execute(pathString, lineParts, redirecting, wait);
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
            execute(fullCommand, lineParts, redirecting, wait);
        }
    }
};


int main(int argc, char** argv) {
    MyShell shell{};
    if (argc > 1) {
        shell.run(argv[1]);
    } else {
        shell.run();
    }

    return 0;
}