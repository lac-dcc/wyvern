cmake_minimum_required(VERSION 3.4)
project(Lazyfication)

if(NOT DEFINED ENV{LLVM_HOME})
	message(FATAL_ERROR "$LLVM_HOME is not defined")

else()
	set(ENV{LLVM_DIR} $ENV{LLVM_HOME}/lib/cmake/llvm)
endif()

set(CMAKE_CXX_STANDARD 17)

find_package(LLVM REQUIRED CONFIG)
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

add_library(wyinstr SHARED wyinstr.cpp)
target_link_options(wyinstr PUBLIC -static-libstdc++ -static-libgcc -Wl,--version-script=${CMAKE_SOURCE_DIR}/link_script)

add_subdirectory(passes)
