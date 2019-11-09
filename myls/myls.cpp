#include <iostream>

#include <boost/filesystem.hpp>
#include <boost/date_time.hpp>
#include <sys/stat.h>
#include <errno.h>
#include <algorithm>

#define fs boost::filesystem

struct FileInfo {
    fs::path path;
    int size;
    time_t modification_time;
    char fileType; // *@|=/?
};

FileInfo getFileInfo(std::string path) {
    struct stat fileStat;

    while (true) {
        errno = 0;
        int result = lstat(path.c_str(), &fileStat);
        if (result != 0) {
            if (errno == EINTR) continue;
            else if (errno == ENOTDIR || errno == EFAULT || errno == ENOENT)
                throw std::invalid_argument("File " + path + " doesn't exist.");
            else
                throw std::runtime_error("Error getting info of file " + path + ".");
        }
        break;
    }

    FileInfo fileInfo;

    if (S_ISDIR(fileStat.st_mode)) fileInfo.fileType = '/';
    else if (S_ISLNK(fileStat.st_mode)) fileInfo.fileType = '@';
    else if (S_ISSOCK(fileStat.st_mode)) fileInfo.fileType = '=';
    else if (S_ISFIFO(fileStat.st_mode)) fileInfo.fileType = '|';
    else if (S_IXGRP & fileStat.st_mode || S_IXUSR & fileStat.st_mode) fileInfo.fileType = '*';
    else fileInfo.fileType = '?';

    // remove last slash
    if (path[path.size() - 1] == '/') path = path.substr(0, path.size() - 1);
    fileInfo.path = fs::path{path}.lexically_normal();

    fileInfo.modification_time = fileStat.st_mtim.tv_sec;
    fileInfo.size = fileStat.st_size;

    return fileInfo;
}

std::vector<std::string> listDirectory(fs::path path) {
    fs::directory_iterator iterator{path};
    fs::directory_iterator end;
    std::vector<std::string> result;

    while (iterator != end) {
        fs::directory_entry entry = *iterator;
        result.push_back(entry.path().string());
        ++iterator;
    }

    return result;
}

struct Config {
    bool detailed_info = false;
    bool showFileTypes = false;

    char sortBy = 'U';
    bool directoriesFirst = false;
    bool specialFilesSeparately = false;
    bool reversed = false;

    bool recurse = false;
};

inline bool isSpecialFile(FileInfo& info) {
    static std::string specialFiles = "@|=";
    return specialFiles.find(info.fileType) != std::string::npos;
}

void appendInfosForFile(Config& config, fs::path path, std::vector<FileInfo>& infos) {
    FileInfo info = getFileInfo(path.string());

    if (info.fileType == '/') {
        std::vector<std::string> paths = listDirectory(path);
        for (auto &path: paths) infos.push_back(getFileInfo(path));

        if (config.sortBy != 'U') {
            std::sort(infos.begin(), infos.end(), [&config](FileInfo &a, FileInfo &b) {
                if (config.directoriesFirst) {
                    if (a.fileType == '/' && b.fileType != '/') return true;
                    if (a.fileType != '/' && b.fileType == '/') return false;
                }
                std::string specialFiles = "@|=";
                if (config.specialFilesSeparately) {
                    if (!isSpecialFile(a) && isSpecialFile(b)) return true;
                    if (isSpecialFile(a) && !isSpecialFile(b)) return false;
                }

                if (config.sortBy == 'S') return a.size < b.size;
                if (config.sortBy == 't') return a.modification_time < b.modification_time;
                if (config.sortBy == 'X') return a.path.extension() < b.path.extension();
                return a.path.filename() < b.path.filename(); // defaults to 'N'
            });
        }

        if (config.reversed) {
            std::reverse(infos.begin(), infos.end());
        }
    } else {
        infos.push_back(info);
    }
}

void showInfo(Config& config, std::vector<std::string> paths) {
    if (paths.empty()) paths.emplace_back(".");

    std::vector<FileInfo> infos;
    for (auto& path: paths) appendInfosForFile(config, path, infos);

    for (auto& info: infos) {
        std::cout << (config.showFileTypes || info.fileType == '/' ? info.fileType : ' ');

        std::string name = info.path.filename().string();
        if (config.detailed_info) {
            boost::posix_time::ptime date = boost::posix_time::from_time_t(info.modification_time);

            std::ios initialConfig(nullptr);
            initialConfig.copyfmt(std::cout);
            std::cout << std::setw(30) << std::left << name
                      << std::setw(14) << info.size
                      << std::setw(14) << date;
            std::cout << std::endl;
            std::cout.copyfmt(initialConfig);
        } else {
            std::cout << name << " ";
        }
    }
    if (!infos.empty()) std::cout << std::endl;

    // recurse over other directories
    for (auto& info: infos) {
        if (config.recurse && info.fileType == '/') {
            std::cout << std::endl << info.path.string() << ":" << std::endl;
            showInfo(config, {info.path.string()});
        }
    }
}

int main(int argc, char** argv) {
    Config config;
    std::vector<std::string> paths{};

    bool files = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = std::string(argv[i]);
        if (arg == "-h" || arg == "--help") {
            // TODO print help
            return 0;
        }
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = std::string(argv[i]);
        // all further parameters are files
        if (arg == "--") {
            files = true;
        }
        // is a file
        if (files) {
            paths.push_back(arg);
        }
        else if (arg.length() > 2 && arg[0] == '"' && arg[arg.length() - 1] == '"') {
            paths.push_back(arg.substr(1, arg.length() - 2));
        }
        else if (arg == "-l") {
            config.detailed_info = true;
        }
        else if (arg.substr(0, 7) == "--sort=") {
            for (char c: arg.substr(7)) {
                if (std::string("UNStX").find(c) != std::string::npos) {
                    config.sortBy = c;
                } else if (c == 'D') {
                    config.directoriesFirst = true;
                } else if (c == 's') {
                    config.specialFilesSeparately = true;
                } else {
                    std::cerr << "Invalid value for sorting: " << c << std::endl;
                    return 2;
                }
            }
        }
        else if (arg == "-r") {
            config.reversed = true;
        }
        else if (arg == "-R") {
            config.recurse = true;
        }
        else if (arg == "-F") {
            config.showFileTypes = true;
        }
        else {
            paths.push_back(arg);
        }
    }

    for (auto &path: paths) {
        if (!fs::exists(path)) {
            std::cerr << "File doesn't exist: " << path << std::endl;
            return 1;
        }
    }

    try {
        showInfo(config, paths);
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 3;
    }

    return 0;
}