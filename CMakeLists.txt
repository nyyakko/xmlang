cmake_minimum_required(VERSION 3.25)

project(xmlang LANGUAGES CXX)

include(cmake/get_cpm.cmake)

CPMAddPackage(URI "gh:nyyakko/LibError#master"       EXCLUDE_FROM_ALL YES)
CPMAddPackage(URI "gh:nyyakko/LibEnum#master"        EXCLUDE_FROM_ALL YES)
CPMAddPackage(URI "gh:nyyakko/LibCoro#master"        EXCLUDE_FROM_ALL YES)
CPMAddPackage(URI "gh:fmtlib/fmt#10.2.1"             EXCLUDE_FROM_ALL YES)
CPMAddPackage(URI "gh:nlohmann/json@3.12.0"          EXCLUDE_FROM_ALL YES)
CPMAddPackage(URI "gh:nyyakko/argparse#master"       EXCLUDE_FROM_ALL YES)

include(cmake/static_analyzers.cmake)

set(xmlang_CompilerOptions ${xmlang_CompilerOptions} -Wno-gnu-statement-expression-from-macro-expansion)
# set(xmlang_LinkerOptions ${xmlang_LinkerOptions})

set(xmlang_ExternalLibraries
    LibError::LibError
    LibEnum::LibEnum
    LibCoro::LibCoro
    nlohmann_json::nlohmann_json
    argparse::argparse
    fmt::fmt
)

add_subdirectory(xmlang)

