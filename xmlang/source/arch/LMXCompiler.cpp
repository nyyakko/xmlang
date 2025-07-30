#include "arch/LMXCompiler.hpp"

#include <liberror/Try.hpp>

#include <regex>
#include <fstream>

using namespace liberror;

static Result<std::string> compile_return(ReturnStmt*)
{
    return "0x05";
}

static Result<std::string> compile_let(LetStmt* letStmt)
{
    std::string code {};

    auto expression = static_cast<Expression*>(letStmt->value.get());

    switch (expression->expr_type())
    {
    case Expression::Type::LITERAL: {
        auto literal = static_cast<LiteralExpr*>(expression);
        std::stringstream stream(literal->value);
        int number; stream >> number;
        code += fmt::format("0x00 {:#04x} 0x02", number);
        break;
    }
    }

    return code;
}

static Result<std::string> compile_argument(std::vector<LetStmt*> const& variables, ArgumentStmt* argumentStmt)
{
    std::string code {};

    auto expression = static_cast<Expression*>(argumentStmt->value.get());

    assert("FIXME: only compiles literal expressions for now" && expression->expr_type() == Expression::Type::LITERAL);

    auto literal = static_cast<LiteralExpr*>(expression);

    std::regex pattern(R"((\$\{([a-zA-Z]+)\}))");
    std::smatch match;

    if (!std::regex_search(literal->value, match, pattern))
    {
        return {};
    }

    auto variable = std::find_if(variables.begin(), variables.end(), [&] (LetStmt* node) {
        return node->name == match.str(2);
    });

    assert("FIXME: only compiles local variables for now" && variable != variables.end());

    code += fmt::format("0x01 0x02 {:#04x} 0x01", std::distance(variables.begin(), variable));

    return code;
}

static Result<std::string> compile_call(std::vector<LetStmt*> const& variables, CallStmt* callStmt)
{
    std::string code {};

    for (auto const& child : callStmt->arguments)
    {
        code += TRY(compile_argument(variables, static_cast<ArgumentStmt*>(child.get()))) + " ";
    }

    assert("FIXME: only compiles println for now" && callStmt->who == "println");

    code += fmt::format("0x04 0x00");

    return code;
}

static Result<std::string> compile_function(FunctionDecl* functionDecl)
{
    std::string code {};

    // code += fmt::format("FUNCTION {}:\n", functionDecl->name);

    std::vector<LetStmt*> variables {};

    for (auto const& child : functionDecl->scope)
    {
        if (static_cast<Statement*>(child.get())->stmt_type() == Statement::Type::LET)
        {
            variables.push_back(static_cast<LetStmt*>(child.get()));
        }
    }

    for (auto const& child : functionDecl->scope)
    {
        auto statement = static_cast<Statement*>(child.get());

        switch (statement->stmt_type())
        {
        case Statement::Type::CALL: {
            code += TRY(compile_call(variables, static_cast<CallStmt*>(statement))) + " ";
            break;
        }
        case Statement::Type::EXPRESSION: {
            assert("UNIMPLEMENTED" && false);
            break;
        }
        case Statement::Type::LET: {
            code += TRY(compile_let(static_cast<LetStmt*>(statement))) + " ";
            break;
        }
        case Statement::Type::RETURN: {
            code += TRY(compile_return(static_cast<ReturnStmt*>(statement))) + " ";
            break;
        }
        }
    }

    return code;
}

static Result<std::string> compile_program(ProgramDecl* programDecl)
{
    std::string code {};

    for (auto const& child : programDecl->scope)
    {
        if (child->node_type() == Node::Type::DECLARATION &&
            static_cast<Declaration*>(child.get())->decl_type() == Declaration::Type::FUNCTION)
        {
            code += TRY(compile_function(static_cast<FunctionDecl*>(child.get())));
        }
    }

    return code;
}

Result<void> arch::lmx::compile(const std::unique_ptr<Node>& ast)
{
    std::vector<uint32_t> program;

    {
        auto instructions = TRY(compile_program(static_cast<ProgramDecl*>(ast.get())));
        std::stringstream stream(instructions);

        for (uint32_t instruction; stream >> std::hex >> instruction; )
        {
            program.push_back(instruction);
        }
    }

    {
        std::ofstream stream("out.lmx", std::ios::binary);

        for (auto instruction : program)
        {
            stream.write(reinterpret_cast<char const*>(&instruction), sizeof(uint8_t));
        }
    }

    return {};
}
