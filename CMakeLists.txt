cmake_minimum_required(VERSION 3.14)
project(myshell)

set(CMAKE_CXX_STANDARD 14)

find_package(Boost COMPONENTS system filesystem REQUIRED)

add_executable(myshell main.cpp)
target_link_libraries(myshell
   ${Boost_FILESYSTEM_LIBRARY}
   ${Boost_SYSTEM_LIBRARY}
)
