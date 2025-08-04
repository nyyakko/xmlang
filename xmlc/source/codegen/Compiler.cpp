#include "codegen/Compiler.hpp"

#include <fmt/core.h>
#include <liberror/Try.hpp>
#include <regex>

using namespace liberror;

static std::map<std::string, int32_t> dataSegmentOffsets_g;

Result<std::string> generate_data_segment(std::unique_ptr<Node> const& node)
{
    std::string code;

    switch (node->node_type())
    {
    case Node::Type::STATEMENT: {
        auto statement = static_cast<Statement const*>(node.get());

        static int32_t dataSegmentBytes = 0;

        switch (statement->stmt_type())
        {
        case Statement::Type::ARG: {
            auto value = TRY(generate_data_segment(static_cast<ArgStmt const*>(statement)->value));

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
            auto value = TRY(generate_data_segment(letStmt->value));

            if (letStmt->type == "string")
            {
                dataSegmentOffsets_g.insert({ letStmt->name, dataSegmentBytes });
            }

            dataSegmentBytes += 4 + value.size();

            return fmt::format("{} {}", value.size(), value);
        }
        case Statement::Type::RETURN: {
            auto retStmt = static_cast<RetStmt const*>(statement);
            if (retStmt->type == "none") return {};

            auto value = TRY(generate_data_segment(retStmt->value));

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
        case Expression::Type::LITERAL: {
            return static_cast<LiteralExpr const*>(expression)->value;
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

Result<std::string> compile_statement(Declaration const* parent, Statement const* statement);

Result<std::string> compile_ret(Declaration const*) { return "RET"; }

Result<std::string> compile_let(Declaration const* , LetStmt const* statement)
{
    std::string code {};

    auto expression = static_cast<Expression const*>(statement->value.get());

    if (expression->expr_type() == Expression::Type::LITERAL)
    {
        auto literal = static_cast<LiteralExpr const*>(expression);

        if (statement->type == "number")
        {
            code += fmt::format("PUSHA {} SCOPE", literal->value);
        }
        else if (statement->type == "string")
        {
            code += fmt::format("PUSHB [.DATA + {}] SCOPE", dataSegmentOffsets_g.at(statement->name));
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

    return code;
}

Result<std::string> compile_arg(Declaration const* declaration, ArgStmt const* statement)
{
    auto expression = static_cast<Expression const*>(statement->value.get());

    if (expression->expr_type() == Expression::Type::LITERAL)
    {
        auto literal = static_cast<LiteralExpr const*>(expression);

        if (std::all_of(literal->value.begin(), literal->value.end(), ::isdigit))
        {
            return fmt::format("PUSHA {} ARGUMENT", literal->value);
        }
        else if (literal->value.starts_with("${") && literal->value.ends_with('}'))
        {
            std::regex pattern(R"((\$\{([\w]*)\}))");
            std::smatch match;

            std::regex_search(literal->value, match, pattern);

            auto maybeVariable = std::find_if(declaration->scope.begin(), declaration->scope.end(), [&] (std::unique_ptr<Node> const& node) {
                return node->node_type() == Node::Type::STATEMENT &&
                       static_cast<Statement const*>(node.get())->stmt_type() == Statement::Type::LET &&
                       static_cast<LetStmt const*>(node.get())->name == match.str(2);
            });

            assert("FIXME: should emit undeclared variable error" && maybeVariable != declaration->scope.end());

            return fmt::format("PUSHB [SCOPE + {}] ARGUMENT", std::distance(declaration->scope.begin(), maybeVariable));
        }

        for (auto const& [key, value] : dataSegmentOffsets_g)
        {
            if (key == literal->value)
                return fmt::format("PUSHB [.DATA + {}] ARGUMENT", value);
        }

        assert("UNREACHABLE" && false);
    }
    else if (expression->expr_type() == Expression::Type::ARITHMETIC)
    {
        assert("UNIMPLEMENTED" && false);
    }
    else if (expression->expr_type() == Expression::Type::LOGICAL)
    {
        assert("UNIMPLEMENTED" && false);
    }

    assert("UNREACHABLE" && false);
}

Result<std::string> compile_call(Declaration const* parent, CallStmt const* statement)
{
    std::string code {};

    for (auto const& child : statement->arguments)
    {
        code += TRY(compile_arg(parent, static_cast<ArgStmt const*>(child.get())));
        code += '\n';
    }

    std::array static constexpr INTRINSICS {
        "print", "println"
    };

    auto maybeIntrinsic = std::find(INTRINSICS.begin(), INTRINSICS.end(), statement->who);

    if (maybeIntrinsic == INTRINSICS.end())
    {
        code += fmt::format("CALLA {}", statement->who);
    }
    else
    {
        code += fmt::format("CALLB {}", *maybeIntrinsic);
    }

    for (auto const& _ : statement->arguments)
    {
        code += '\n';
        code += "POP ARGUMENT";
    }

    return code;
}

Result<std::string> compile_if(Declaration const* parent, IfStmt const*)
{
    (void)parent;
    assert("UNIMPLEMENTED" && false);
    return {};
}

Result<std::string> compile_statement(Declaration const* parent, Statement const* statement)
{
    if (statement->stmt_type() == Statement::Type::LET)
    {
        return compile_let(parent, static_cast<LetStmt const*>(statement));
    }

    if (statement->stmt_type() == Statement::Type::CALL)
    {
        return compile_call(parent, static_cast<CallStmt const*>(statement));
    }

    if (statement->stmt_type() == Statement::Type::RETURN)
    {
        return compile_ret(parent);
    }

    if (statement->stmt_type() == Statement::Type::IF)
    {
        return compile_if(parent, static_cast<IfStmt const*>(statement));
    }

    return {};
}

Result<std::string> compile_declaration(Declaration const* declaration);

Result<std::string> compile_function(FunctionDecl const* declaration)
{
    std::string code = fmt::format("FUNCTION {}:\n\n", declaration->name);

    // cppcheck-suppress [variableScope]
    for (std::string_view separator = ""; auto const& child : declaration->scope)
    {
        code += separator;
        code += TRY(compile_statement(declaration, static_cast<Statement const*>(child.get())));
        separator = "\n";
    }

    return code;
}

Result<std::string> compile_declaration(Declaration const* declaration)
{
    if (declaration->decl_type() == Declaration::Type::FUNCTION)
    {
        return compile_function(static_cast<FunctionDecl const*>(declaration));
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
            code += TRY(compile_declaration(static_cast<Declaration const*>(child.get())));
            prefix = "\n\n";
        }
    }

    if (!code.empty()) code += "\n\n";
    code += "ENTRYPOINT\n\n";

    for (auto const& child : declaration->scope)
    {
        if (child->node_type() == Node::Type::STATEMENT && static_cast<Statement const*>(child.get())->stmt_type() == Statement::Type::CALL)
        {
            code += TRY(compile_statement(declaration, static_cast<Statement const*>(child.get())));
            code += '\n';
        }
    }

    code += "RET";

    return code;
}

Result<std::string> compile(std::unique_ptr<Node> const& ast)
{
    std::string dataSegment = ".DATA\n\n";
    dataSegment += TRY(generate_data_segment(ast));

    std::string codeSegment = "\n\n.CODE\n\n";
    codeSegment += TRY(compile_program(static_cast<ProgramDecl const*>(ast.get())));

    return dataSegment + codeSegment;
}
