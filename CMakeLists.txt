cmake_minimum_required(VERSION 3.14)
project(myshell)

set(CMAKE_CXX_STANDARD 14)

find_package(Boost COMPONENTS system filesystem REQUIRED)
find_library(Readline_LIBRARY NAMES readline)

add_executable(myshell main.cpp)
target_link_libraries(myshell
   ${Boost_FILESYSTEM_LIBRARY}
   ${Boost_SYSTEM_LIBRARY}
   readline
)

set(CMAKE_C_STANDARD 99)
add_executable(mycat mycat.c filef.h)
set_target_properties(mycat PROPERTIES LINKER_LANGUAGE C)
