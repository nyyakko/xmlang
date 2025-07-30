#pragma once

#include "Parser.hpp"

#include <liberror/Result.hpp>

// cppcheck-suppress [unknownMacro]
ENUM_CLASS(CompilerError,
    MISMATCHING_ARGUMENT_COUNT,
    MISMATCHING_ARGUMENT_TYPE
)

liberror::Result<void> compile(std::unique_ptr<Node> const& ast, std::string_view arch = "lmx");
