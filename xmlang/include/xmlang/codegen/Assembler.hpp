#pragma once

#include <string_view>

#include <liberror/Result.hpp>

liberror::Result<void> assemble(std::string_view code);
