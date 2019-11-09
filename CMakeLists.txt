cmake_minimum_required(VERSION 3.14)
project(myshell)

set(CMAKE_CXX_STANDARD 14)

find_package(Boost COMPONENTS system filesystem date_time REQUIRED)
find_library(Readline_LIBRARY NAMES readline)

include_directories(include)

add_library(wildcards src/wildcards.cpp)
target_link_libraries(wildcards ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY})

add_library(CommandPart src/CommandPart.cpp)
add_library(redirectsParser src/redirectsParser.cpp)
add_library(system_read_write src/system_read_write.cpp)

add_executable(myshell src/main.cpp)
target_link_libraries(myshell
        ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY}
        wildcards CommandPart redirectsParser system_read_write
        readline
)

set(CMAKE_C_STANDARD 99)
add_executable(mycat mycat/mycat.c mycat/filef.h mycat/filef.c)
set_target_properties(mycat PROPERTIES LINKER_LANGUAGE C)

add_executable(myls myls/myls.cpp)
target_link_libraries(myls
        ${Boost_FILESYSTEM_LIBRARY}
        ${Boost_SYSTEM_LIBRARY}
        ${Boost_DATE_TIME_LIBRARY}
)