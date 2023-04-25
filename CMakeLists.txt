cmake_minimum_required(VERSION 3.15)
set(CMAKE_CXX_STANDARD 17)
project(mypass)

set(LLVM_HOME "C:/msys64/clang64")
set(LLVM_DIR "${LLVM_HOME}/lib/cmake/llvm")
find_package(LLVM REQUIRED CONFIG)

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})
include_directories(${LLVM_INCLUDE_DIRS})

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

llvm_add_library(pass MODULE

        pass_register.cc
        pass_bof_rename_func.h
)
