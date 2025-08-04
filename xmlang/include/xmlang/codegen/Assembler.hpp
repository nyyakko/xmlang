#pragma once

#include <string_view>

#include <liberror/Result.hpp>

liberror::Result<std::vector<uint8_t>> assemble(std::string_view code);
