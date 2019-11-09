cmake_minimum_required(VERSION 3.14)
project(myshell)

set(CMAKE_CXX_STANDARD 14)

find_package(Boost COMPONENTS system filesystem REQUIRED)
find_library(Readline_LIBRARY NAMES readline)

add_executable(myshell main.cpp wildcards.h wildcards.cpp CommandPart.cpp CommandPart.h redirectsParser.h system_read_write.h system_read_write.cpp)
target_link_libraries(myshell
   ${Boost_FILESYSTEM_LIBRARY}
   ${Boost_SYSTEM_LIBRARY}
   readline
)

set(CMAKE_C_STANDARD 99)
add_executable(mycat mycat/mycat.c mycat/filef.h mycat/filef.c)
set_target_properties(mycat PROPERTIES LINKER_LANGUAGE C)
