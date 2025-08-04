#pragma once

#include "Parser.hpp"

#include <liberror/Result.hpp>

#include <string>

liberror::Result<std::string> compile(std::unique_ptr<Node> const& ast);
