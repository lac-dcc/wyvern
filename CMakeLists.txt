cmake_minimum_required(VERSION 3.4)
project(Lazyfication)

set(CMAKE_CXX_STANDARD 17)

find_package(LLVM REQUIRED CONFIG)
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

add_library(wyinstr SHARED wyinstr.cpp)
set_target_properties(wyinstr PROPERTIES
	COMPILE_FLAGS "-g -O3"
)
target_link_options(wyinstr PUBLIC -static-libstdc++ -static-libgcc -lpthread -Wl,--version-script=${CMAKE_SOURCE_DIR}/link_script)

add_subdirectory(passes)
