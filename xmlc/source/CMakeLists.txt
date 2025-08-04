add_subdirectory(codegen)

set(DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(xmlc_SourceFiles ${xmlc_SourceFiles}
    "${DIR}/Main.cpp"
    "${DIR}/Lexer.cpp"
    "${DIR}/Parser.cpp"

    PARENT_SCOPE
)

