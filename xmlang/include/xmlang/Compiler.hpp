#pragma once

#include "Parser.hpp"

#include <liberror/Result.hpp>

liberror::Result<void> compile(std::unique_ptr<Node> const& ast, std::string_view arch = "lmx");
