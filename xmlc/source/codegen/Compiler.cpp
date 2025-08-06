#include "codegen/Compiler.hpp"
#include "Parser.hpp"

#include <fmt/core.h>
#include <liberror/Try.hpp>
#include <regex>

using namespace liberror;

static std::map<std::string, int32_t> dataSegmentOffsets_g;

struct Intrinsic
{
    std::string_view name;
    std::string_view type;
};

static constexpr std::array intrinsics_g {
    Intrinsic { "print", "none" },
    Intrinsic { "println", "none" }
};

Result<std::string> generate_data_segment(std::unique_ptr<Node> const& node)
{
    std::string code;

    static int32_t dataSegmentBytes = 0;

    switch (node->node_type())
    {
    case Node::Type::STATEMENT: {
        auto statement = static_cast<Statement const*>(node.get());

        switch (statement->stmt_type())
        {
        case Statement::Type::CALL: {
            for (std::string_view prefix = ""; auto const& child : static_cast<CallStmt const*>(statement)->arguments)
            {
                auto value = TRY(generate_data_segment(child));
                if (value.empty()) continue;
                code += prefix;
                code += value;
                prefix = "\n";
            }

            return code;
        }
        case Statement::Type::LET: {
            auto letStmt = static_cast<LetStmt const*>(statement);
            if (letStmt->type != "string") return {};

            auto value = TRY(generate_data_segment(letStmt->value));

            dataSegmentOffsets_g.insert({ letStmt->name, dataSegmentBytes });
            dataSegmentBytes += 4 + value.size();

            return fmt::format("{} {}", value.size(), value);
        }
        case Statement::Type::RETURN: {
            auto retStmt = static_cast<RetStmt const*>(statement);
            if (retStmt->type == "none") return {};

            auto value = TRY(generate_data_segment(retStmt->value));
            if (value.empty()) return {};

            if (retStmt->type == "string")
            {
                dataSegmentOffsets_g.insert({ value, dataSegmentBytes });
            }

            dataSegmentBytes += 4 + value.size();

            return fmt::format("{} {}", value.size(), value);
        }
        case Statement::Type::IF: break;
        }

        break;
    }
    case Node::Type::EXPRESSION: {
        auto expression = static_cast<Expression const*>(node.get());

        switch (expression->expr_type())
        {
        case Expression::Type::ARITHMETIC: assert("UNREACHABLE" && false);
        case Expression::Type::LOGICAL: assert("UNREACHABLE" && false);
        case Expression::Type::ARG: {
            auto value = TRY(generate_data_segment(static_cast<ArgExpr const*>(expression)->value));

            if (static_cast<Expression const*>(static_cast<ArgExpr const*>(expression)->value.get())->expr_type() != Expression::Type::LITERAL)
            {
                return value;
            }

            if (!(std::all_of(value.begin(), value.end(), ::isdigit) || (value.starts_with("${") && value.ends_with('}'))))
            {
                dataSegmentOffsets_g.insert({ value, dataSegmentBytes });
            }
            else
            {
                return {};
            }

            dataSegmentBytes += 4 + value.size();

            std::regex pattern(R"((\$\{([\w]*)\}))");
            std::sregex_iterator iterator(value.begin(), value.end(), pattern);

            for (; iterator != std::sregex_iterator{}; iterator = std::next(iterator))
            {
                value.replace(size_t(iterator->position()), iterator->str(1).size(), "{}");
            }

            return fmt::format("{} {}", value.size(), value);
        }
        case Expression::Type::CALL: {
            for (std::string_view prefix = ""; auto const& child : static_cast<CallExpr const*>(expression)->arguments)
            {
                auto value = TRY(generate_data_segment(child));
                if (value.empty()) continue;
                code += prefix;
                code += value;
                prefix = "\n";
            }

            return code;
        }
        case Expression::Type::LITERAL: {
            auto const& value = static_cast<LiteralExpr const*>(expression)->value;
            if (value.starts_with("${") && value.ends_with('}')) return {};
            return value;
        }
        }

        break;
    }
    case Node::Type::DECLARATION: {
        auto declaration = static_cast<Declaration const*>(node.get());

        for (std::string_view prefix; auto const& child : declaration->scope)
        {
            auto value = TRY(generate_data_segment(child));
            if (value.empty()) continue;
            code += prefix;
            code += value;
            prefix = "\n";
        }

        break;
    }
    }

    return code;
}

Result<std::string> compile_expression(ProgramDecl const* program, Declaration const* parent, Expression const* expression);

Result<std::string> compile_literal_expression(ProgramDecl const*, Declaration const* parent, LiteralExpr const* expression)
{
    std::vector<std::string> variables {};

    if (parent->decl_type() == Declaration::Type::FUNCTION)
    {
        auto const& parameters = static_cast<FunctionDecl const*>(parent)->parameters;
        std::transform(parameters.begin(), parameters.end(), std::back_inserter(variables), [&] (auto&& parameter) { return parameter.first; });
    }

    for (auto const& child : parent->scope)
    {
        if (child->node_type() == Node::Type::STATEMENT && static_cast<Statement const*>(child.get())->stmt_type() == Statement::Type::LET)
        {
            variables.push_back(static_cast<LetStmt const*>(child.get())->name);
        }
    }

    if (std::all_of(expression->value.begin(), expression->value.end(), ::isdigit))
    {
        return fmt::format("push {}", expression->value);
    }
    else if (expression->value.starts_with("${") && expression->value.ends_with('}'))
    {
        std::regex pattern(R"((\$\{([\w]*)\}))");
        std::smatch match;

        std::regex_search(expression->value, match, pattern);

        auto maybeVariable = std::find_if(variables.begin(), variables.end(), [&] (auto&& variable) { return variable == match.str(2); });
        assert("FIXME: should emit undeclared variable error" && maybeVariable != variables.end());

        return fmt::format("load scope[{}]", std::distance(variables.begin(), maybeVariable));
    }
    else if (dataSegmentOffsets_g.contains(expression->value))
    {
        return fmt::format("load .data[{}]", dataSegmentOffsets_g.at(expression->value));
    }

    assert("UNREACHABLE" && false);
}

Result<std::string> compile_arithmetic_expression(ProgramDecl const*, Declaration const* , ArithmeticExpr const* )
{
    assert("UNIMPLEMENTED" && false);
}

Result<std::string> compile_logical_expression(ProgramDecl const*, Declaration const*, LogicalExpr const*)
{
    assert("UNIMPLEMENTED" && false);
}

Result<std::string> compile_arg_expression(ProgramDecl const* program, Declaration const* parent, ArgExpr const* statement)
{
    return TRY(compile_expression(program, parent, static_cast<Expression const*>(statement->value.get())));
}

Result<std::string> compile_call_expression(ProgramDecl const* program, Declaration const* parent, CallExpr const* expression)
{
    std::string code {};

    for (auto const& child : expression->arguments)
    {
        code += TRY(compile_arg_expression(program, parent, static_cast<ArgExpr const*>(child.get())));
        code += '\n';
    }

    auto maybeIntrinsic = std::find_if(intrinsics_g.begin(), intrinsics_g.end(), [&] (auto&& intrinsic) {
        return intrinsic.name == expression->who;
    });

    if (maybeIntrinsic == intrinsics_g.end())
    {
        code += fmt::format("call {}", expression->who);
    }
    else
    {
        code += fmt::format("call {}", maybeIntrinsic->name);
    }

    return code;
}

Result<std::string> compile_expression(ProgramDecl const* program, Declaration const* parent, Expression const* expression)
{
    switch (expression->expr_type())
    {
    case Expression::Type::ARG: break;
    case Expression::Type::LITERAL: return compile_literal_expression(program, parent, static_cast<LiteralExpr const*>(expression));
    case Expression::Type::LOGICAL: return compile_logical_expression(program, parent, static_cast<LogicalExpr const*>(expression));
    case Expression::Type::ARITHMETIC: return compile_arithmetic_expression(program, parent, static_cast<ArithmeticExpr const*>(expression));
    case Expression::Type::CALL: return compile_call_expression(program, parent, static_cast<CallExpr const*>(expression));
    }

    assert("UNREACHABLE" && false);
}

Result<std::string> compile_statement(ProgramDecl const* program, Declaration const* parent, Statement const* statement);

Result<std::string> compile_ret_statement(ProgramDecl const* program, Declaration const* parent, RetStmt const* statement)
{
    std::string code {};

    if (statement->value)
    {
        code += TRY(compile_expression(program, parent, static_cast<Expression const*>(statement->value.get())));
        code += '\n';
    }

    code += "ret";

    return code;
}

Result<std::string> compile_let_statement(ProgramDecl const*, Declaration const* parent, LetStmt const* statement)
{
    std::string code {};

    auto expression = static_cast<Expression const*>(statement->value.get());

    if (expression->expr_type() == Expression::Type::LITERAL)
    {
        auto literal = static_cast<LiteralExpr const*>(expression);

        if (statement->type == "number")
        {
            code += fmt::format("push {}", literal->value);
        }
        else if (statement->type == "string")
        {
            code += fmt::format("load .data[{}]", dataSegmentOffsets_g.at(statement->name));
        }
    }
    else if (expression->expr_type() == Expression::Type::ARITHMETIC)
    {
        assert("UNIMPLEMENTED" && false);
    }
    else if (expression->expr_type() == Expression::Type::LOGICAL)
    {
        assert("UNIMPLEMENTED" && false);
    }

    auto variable = std::find_if(parent->scope.begin(), parent->scope.end(), [&] (std::unique_ptr<Node> const& node) {
        return node->node_type() == Node::Type::STATEMENT &&
               static_cast<Statement const*>(node.get())->stmt_type() == Statement::Type::LET &&
               static_cast<LetStmt const*>(node.get())->name == statement->name;
    });

    assert(variable != parent->scope.end());

    code += fmt::format("\nstore scope[{}]", std::distance(parent->scope.begin(), variable));

    return code;
}

Result<std::string> compile_call_statement(ProgramDecl const* program, Declaration const* parent, CallStmt const* statement)
{
    std::string code {};

    for (auto const& child : statement->arguments)
    {
        code += TRY(compile_arg_expression(program, parent, static_cast<ArgExpr const*>(child.get())));
        code += '\n';
    }

    auto maybeIntrinsic = std::find_if(intrinsics_g.begin(), intrinsics_g.end(), [&] (auto&& intrinsic) {
        return intrinsic.name == statement->who;
    });

    if (maybeIntrinsic == intrinsics_g.end())
    {
        code += fmt::format("call {}", statement->who);

        auto function = std::find_if(program->scope.begin(), program->scope.end(), [&] (std::unique_ptr<Node> const& node) {
            return node->node_type() == Node::Type::DECLARATION &&
                   static_cast<Declaration const*>(node.get())->decl_type() == Declaration::Type::FUNCTION &&
                   static_cast<FunctionDecl const*>(node.get())->name == statement->who;
        });

        assert(function != program->scope.end());

        if (static_cast<FunctionDecl const*>(function->get())->type != "none")
        {
            code += '\n';
            code += "pop";
        }
    }
    else
    {
        code += fmt::format("call {}", maybeIntrinsic->name);

        if (maybeIntrinsic->type != "none")
        {
            code += '\n';
            code += "pop";
        }
    }

    return code;
}

Result<std::string> compile_if_statement(ProgramDecl const*, Declaration const*, IfStmt const*)
{
    assert("UNIMPLEMENTED" && false);
    return {};
}

Result<std::string> compile_statement(ProgramDecl const* program, Declaration const* parent, Statement const* statement)
{
    if (statement->stmt_type() == Statement::Type::LET)
    {
        return compile_let_statement(program, parent, static_cast<LetStmt const*>(statement));
    }

    if (statement->stmt_type() == Statement::Type::CALL)
    {
        return compile_call_statement(program, parent, static_cast<CallStmt const*>(statement));
    }

    if (statement->stmt_type() == Statement::Type::RETURN)
    {
        return compile_ret_statement(program, parent, static_cast<RetStmt const*>(statement));
    }

    if (statement->stmt_type() == Statement::Type::IF)
    {
        return compile_if_statement(program, parent, static_cast<IfStmt const*>(statement));
    }

    return {};
}

Result<std::string> compile_declaration(ProgramDecl const* program, Declaration const* declaration);

Result<std::string> compile_function_declaration(ProgramDecl const* program, FunctionDecl const* declaration)
{
    std::string code = fmt::format("function {}\n\n", declaration->name);

    // cppcheck-suppress [variableScope]
    for (std::string_view separator = ""; auto const& child : declaration->scope)
    {
        code += separator;
        code += TRY(compile_statement(program, declaration, static_cast<Statement const*>(child.get())));
        separator = "\n";
    }

    return code;
}

Result<std::string> compile_declaration(ProgramDecl const* program, Declaration const* declaration)
{
    if (declaration->decl_type() == Declaration::Type::FUNCTION)
    {
        return compile_function_declaration(program, static_cast<FunctionDecl const*>(declaration));
    }

    return {};
}

Result<std::string> compile_program(ProgramDecl const* declaration)
{
    std::string code {};

    for (std::string_view prefix = ""; auto const& child : declaration->scope)
    {
        if (child->node_type() == Node::Type::DECLARATION)
        {
            code += prefix;
            code += TRY(compile_declaration(declaration, static_cast<Declaration const*>(child.get())));
            prefix = "\n\n";
        }
    }

    if (!code.empty()) code += "\n\n";
    code += "entrypoint\n\n";

    for (auto const& child : declaration->scope)
    {
        if (child->node_type() == Node::Type::STATEMENT && static_cast<Statement const*>(child.get())->stmt_type() == Statement::Type::CALL)
        {
            code += TRY(compile_statement(declaration, declaration, static_cast<Statement const*>(child.get())));
            code += '\n';
        }
    }

    code += "ret";

    return code;
}

Result<std::string> compile(std::unique_ptr<Node> const& ast)
{
    std::string dataSegment;
    auto data = TRY(generate_data_segment(ast));
    if (!data.empty())
    {
        dataSegment += ".data\n";
        dataSegment += fmt::format("\n{}\n\n", data);
    }

    std::string codeSegment = ".code\n";
    auto code = TRY(compile_program(static_cast<ProgramDecl const*>(ast.get())));
    if (!code.empty()) codeSegment += fmt::format("\n{}", code);

    return dataSegment + codeSegment;
}
