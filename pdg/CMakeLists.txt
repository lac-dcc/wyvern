cmake_minimum_required(VERSION 3.4.3)
project(pdg)
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LT_LLVM_INSTALL_DIR}")

message("LLVM STATUS:
  Definitions ${LLVM_DEFINITIONS}
  Includes    ${LLVM_INCLUDE_DIRS}
  Libraries   ${LLVM_LIBRARY_DIRS}
  Targets     ${LLVM_TARGETS_TO_BUILD}"
)

include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Use the same C++ standard as LLVM does
set(CMAKE_CXX_STANDARD 14 CACHE STRING "")

# Build type
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE
      STRING "Build type (default Debug):" FORCE)
endif()

# LLVM is normally built without RTTI. Be consistent with that.
if(NOT LLVM_ENABLE_RTTI)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall\
    -fdiagnostics-color=always -Wno-everything")


include_directories("include")
include_directories("src")
# include_directories("sea-dsa/include")

file(GLOB SOURCES "src/*.cpp")
file(GLOB HEADERS "include/*.hh")

add_library(pdg MODULE
    ${HEADERS}
    ${SOURCES}
)

add_library(pdg_shared STATIC ${SOURCES})

#
# Turn on C++11, turn off C++ RTTI.
#
# target_compile_features(pdg PRIVATE cxx_range_for cxx_auto_type)
# set_target_properties(pdg PROPERTIES COMPILE_FLAGS "-fno-rtti")

#
# OS X-specific configuration
#
if(APPLE)
    set_target_properties(pdg PROPERTIES LINK_FLAGS "-undefined dynamic_lookup")
endif(APPLE)

