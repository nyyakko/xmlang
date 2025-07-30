#include "Compiler.hpp"

#include "arch/LMXCompiler.hpp"

#include <liberror/Try.hpp>

using namespace liberror;

Result<void> compile(std::unique_ptr<Node> const& ast, std::string_view arch)
{
    if (arch == "lmx")
    {
        TRY(arch::lmx::compile(ast));
    }

    return {};
}
