cmake_minimum_required(VERSION 3.4)
project(Lazyfication)

if(NOT DEFINED ENV{LLVM_HOME})
	message(FATAL_ERROR "$LLVM_HOME is not defined")

else()
	set(ENV{LLVM_DIR} $ENV{LLVM_HOME}/lib/cmake/llvm)
endif()

find_package(LLVM REQUIRED CONFIG)
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
if (${LLVM_VERSION_MAJOR} VERSION_GREATER_EQUAL 10)
	set(CMAKE_CXX_STANDARD 14)
endif ()

add_subdirectory(find-lazyfiable)
