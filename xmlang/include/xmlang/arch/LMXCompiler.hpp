#pragma once

#include "Parser.hpp"

#include <liberror/Result.hpp>

namespace arch::lmx {

liberror::Result<void> compile(std::unique_ptr<Node> const& ast);

}
