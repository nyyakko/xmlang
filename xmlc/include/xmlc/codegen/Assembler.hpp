#pragma once

#include <liberror/Result.hpp>

#include <vector>

liberror::Result<std::vector<uint8_t>> assemble(std::string const& code);
