#include "Parser.hpp"

#include <magic_enum/magic_enum.hpp>
#include <libcoro/Generator.hpp>
#include <liberror/Result.hpp>
#include <liberror/Try.hpp>

#include <iostream>
#include <fstream>

using namespace libcoro;
using namespace liberror;

auto static constexpr RED = "\033[31m";
auto static constexpr GREEN = "\033[32m";
auto static constexpr BLUE = "\033[34m";
auto static constexpr YELLOW = "\033[33m";
auto static constexpr RESET = "\033[00m";

enum class ParserError
{
    UNEXPECTED_TOKEN_REACHED,
    EXPECTED_TOKEN_MISSING,
    ENCLOSING_TOKEN_MISSING,
    ENCLOSING_TOKEN_MISMATCH,
    UNEXPECTED_END_OF_FILE,
    MISSING_RETURN_STATEMENT,
};

static auto hadAnError_g = false;

static Generator<std::string> next_line(std::filesystem::path const& path)
{
    std::ifstream stream(path);

    for (std::string line; std::getline(stream, line); )
    {
        co_yield line;
    }

    co_return;
}

static void emit_parser_error(ParserError const& error, std::vector<std::pair<Token, std::string_view>> const& issues)
{
    hadAnError_g = true;

    std::vector<std::string> lines {};

    for (auto const& line : next_line(issues.at(0).first.location.first))
    {
        // cppcheck-suppress [useStlAlgorithm]
        lines.push_back(line);
    }

    std::cout << RED << "[error]: " << RESET;

    switch (error)
    {
    case ParserError::UNEXPECTED_TOKEN_REACHED: { std::cout << "unexpected token"; break; }
    case ParserError::EXPECTED_TOKEN_MISSING: { std::cout << "missing expected token"; break; }
    case ParserError::ENCLOSING_TOKEN_MISSING: { std::cout << " missing enclosing token"; break; }
    case ParserError::ENCLOSING_TOKEN_MISMATCH: { std::cout << "mismatching tokens found"; break; }
    case ParserError::MISSING_RETURN_STATEMENT: { std::cout << "missing return statement"; break; }
    case ParserError::UNEXPECTED_END_OF_FILE: { std::cout << "unexpected end of file"; break; }
    }

    std::cout << '\n';

    for (auto const& [token, message] : issues)
    {
        auto& [data, type, location, depth] = token;
        auto& [file, position] = location;
        auto& [line, column] = position;

        auto beforeToken = lines.at(line).substr(0, 1+column-data.size());
        auto afterToken  = column ? lines.at(line).substr(1+column) : "";

        std::cout << '\n';
        std::cout << "at " << file.string() << ':' << line+1 << ':' << beforeToken.size() + 1<< '\n';
        std::cout << '\n';

        auto index = beforeToken.find_first_not_of(' ');

        if (index != std::string::npos)
        {
            beforeToken = beforeToken.substr(index);
        }
        else if (std::all_of(beforeToken.begin(), beforeToken.end(), ::isspace))
        {
            beforeToken = "";
        }

        std::cout << GREEN << std::right << std::setw(4) << line+1 << RESET << " | " << beforeToken << BLUE << token.data << RESET << afterToken << '\n';
        std::cout << "    " << " | " << std::string(beforeToken.size(), ' ') << RED << std::string(data.size(), '^') << RESET << ' ' << message << '\n';
    }

    std::cout << '\n';
}

enum class ParserWarning
{
    UNEXPECTED_TOKEN_POSITION
};

static void emit_parser_warning(ParserWarning const& warning, std::vector<std::pair<Token, std::string_view>> const& issues)
{
    std::vector<std::string> lines {};

    for (auto const& line : next_line(issues.at(0).first.location.first))
    {
        // cppcheck-suppress [useStlAlgorithm]
        lines.push_back(line);
    }

    std::cout << YELLOW << "[warning]: " << RESET;

    switch (warning)
    {
    case ParserWarning::UNEXPECTED_TOKEN_POSITION: { std::cout << "unexpected token position"; break; }
    }

    std::cout << '\n';

    for (auto const& [token, message] : issues)
    {
        auto& [data, type, location, depth] = token;
        auto& [file, position] = location;
        auto& [line, column] = position;

        auto beforeToken = lines.at(line).substr(0, 1+column-data.size());
        auto afterToken  = column ? lines.at(line).substr(1+column) : "";

        std::cout << '\n';
        std::cout << "at " << file.string() << ':' << line+1 << ':' << beforeToken.size() + 1<< '\n';
        std::cout << '\n';

        auto index = beforeToken.find_first_not_of(' ');

        if (index != std::string::npos)
        {
            beforeToken = beforeToken.substr(index);
        }
        else if (std::all_of(beforeToken.begin(), beforeToken.end(), ::isspace))
        {
            beforeToken = "";
        }

        std::cout << GREEN << std::right << std::setw(4) << line+1 << RESET << " | " << beforeToken << BLUE << token.data << RESET << afterToken << '\n';
        std::cout << "    " << " | " << std::string(beforeToken.size(), ' ') << YELLOW << std::string(data.size(), '^') << RESET << ' ' << message << '\n';
    }
}

static bool expect(std::vector<Token> const& tokens, int cursor, Token::Type type)
{
    if (cursor == -1 || tokens.at(static_cast<size_t>(cursor)).type != type)
    {
        return false;
    }

    return true;
}

static bool expect(std::vector<Token> const& tokens, int cursor, Token::Type type, std::string_view data)
{
    if (cursor == -1 || tokens.at(static_cast<size_t>(cursor)).type != type)
    {
        return false;
    }

    if (cursor == -1 || (tokens.at(static_cast<size_t>(cursor)).type == type && tokens.at(static_cast<size_t>(cursor)).data != data))
    {
        return false;
    }

    return true;
}

static Token peek(std::vector<Token> const& tokens, int cursor, int distance = 0)
{
    return tokens.at(static_cast<size_t>(cursor - distance));
}

static Token advance(std::vector<Token> const& tokens, int& cursor)
{
    return tokens.at(static_cast<size_t>(cursor--));
}

static Result<Token> advance(std::vector<Token> const& tokens, int& cursor, Token::Type type)
{
    if (expect(tokens, cursor, type))
    {
        return tokens.at(static_cast<size_t>(cursor--));
    }

    return make_error({});
}

static Result<Token> advance(std::vector<Token> const& tokens, int& cursor, Token::Type type, std::string_view data)
{
    if (expect(tokens, cursor, type, data))
    {
        return tokens.at(static_cast<size_t>(cursor--));
    }

    return make_error({});
}

static bool is_next_statement(std::vector<Token> const& tokens, int cursor)
{
    return
        peek(tokens, cursor, 1).data == "let" ||
        peek(tokens, cursor, 1).data == "call" ||
        peek(tokens, cursor, 1).data == "arg" ||
        peek(tokens, cursor, 1).data == "return"
        ;
}

static bool is_next_declaration(std::vector<Token> const& tokens, int cursor)
{
    return
        peek(tokens, cursor, 1).data == "function"
        ;
}

static void synchronize(std::vector<Token> const& tokens, Token const& token, int& cursor)
{
    while (
        cursor > 2 &&
        !(
            (peek(tokens, cursor, 0).type == Token::Type::LEFT_ANGLE && peek(tokens, cursor, 1).type == Token::Type::KEYWORD &&
             peek(tokens, cursor, 0).depth == token.depth + 1) ||
             peek(tokens, cursor, 0).depth == token.depth
        )
    )
    {
        advance(tokens, cursor);
    }
}

static Result<std::unique_ptr<Node>> parse_expression(std::vector<Token> const& tokens, int& cursor)
{
    if (expect(tokens, cursor, Token::Type::LITERAL))
    {
        auto literal = std::make_unique<LiteralExpr>();
        literal->value = advance(tokens, cursor).data;
        return literal;
    }

    if (expect(tokens, cursor, Token::Type::KEYWORD))
    {
        assert("UNIMPLEMENTED" && false);
    }

    return {};
}

using Property = std::pair<Token, std::unique_ptr<Node>>;
using Tag = std::pair<Token, std::vector<Property>>;

static Result<Tag> parse_opening_tag(std::vector<Token> const& tokens, int& cursor, std::string_view name)
{
    if (!advance(tokens, cursor, Token::Type::LEFT_ANGLE))
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, {{ peek(tokens, cursor), "was found instead of a '<'" }});
        return make_error({});
    }

    auto tag = advance(tokens, cursor, Token::Type::KEYWORD, name);

    if (!tag)
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, {{ peek(tokens, cursor), "was found instead of a tag" }});
        return make_error({});
    }

    std::vector<Property> properties {};

    while (cursor > 1 && peek(tokens, cursor).type != Token::Type::RIGHT_ANGLE)
    {
        auto propertyName = advance(tokens, cursor, Token::Type::PROPERTY);

        if (!propertyName)
        {
            emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, {{ peek(tokens, cursor), "was found instead of a property" }});
            return make_error({});
        }

        if (!advance(tokens, cursor, Token::Type::EQUAL))
        {
            emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, {{ peek(tokens, cursor), "was found instead of equals" }});
            return make_error({});
        }

        if (!(advance(tokens, cursor, Token::Type::DOUBLE_QUOTE) || advance(tokens, cursor, Token::Type::SINGLE_QUOTE)))
        {
            emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, {{ peek(tokens, cursor), "was found instead of quotes" }});
            return make_error({});
        }

        auto propertyValue = parse_expression(tokens, cursor);

        if (!propertyValue)
        {
            emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, {{ peek(tokens, cursor), "was found instead of a property value" }});
            return make_error({});
        }

        if (!(advance(tokens, cursor, Token::Type::DOUBLE_QUOTE) || advance(tokens, cursor, Token::Type::SINGLE_QUOTE)))
        {
            emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, {{ peek(tokens, cursor), "was found instead of quotes" }});
            return make_error({});
        }

        properties.emplace_back(*propertyName, std::move(*propertyValue));
    }

    if (!advance(tokens, cursor, Token::Type::RIGHT_ANGLE))
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, {{ peek(tokens, cursor), "was found instead of a '>'" }});
        return make_error({});
    }

    return std::pair { *tag, std::move(properties) };
}

static Result<void> parse_closing_tag(std::vector<Token> const& tokens, int& cursor, Token const& tag)
{
    if (!advance(tokens, cursor, Token::Type::LEFT_ANGLE))
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, {{ peek(tokens, cursor), "was found instead of a '<'" }});
        return make_error({});
    }

    if (!advance(tokens, cursor, Token::Type::SLASH))
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, {{ peek(tokens, cursor), "was found instead of a '/'" }});
        return make_error({});
    }

    if (auto closing = advance(tokens, cursor, Token::Type::KEYWORD); !closing)
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, {{ peek(tokens, cursor), "was found instead of a tag" }});
        return make_error({});
    }
    else if (closing->data != tag.data)
    {
        using namespace std::literals;

        emit_parser_error(ParserError::ENCLOSING_TOKEN_MISMATCH, { { tag, "this tag" }, { *closing, "does not match with this one" } });

        return make_error({});
    }

    if (!advance(tokens, cursor, Token::Type::RIGHT_ANGLE))
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, {{ peek(tokens, cursor), "was found instead of '>'" }});
        return make_error({});
    }

    return {};
}

static Result<std::unique_ptr<Node>> parse_statement(std::vector<Token> const& tokens, int& cursor);

static Result<std::unique_ptr<Node>> parse_arg(std::vector<Token> const& tokens, int& cursor)
{
    auto argumentStmt = std::make_unique<ArgStmt>();

    auto [tag, properties] = TRY(parse_opening_tag(tokens, cursor, "arg"));

    argumentStmt->token = tag;

    auto maybeValue = std::ranges::find_if(properties, [] (auto&& current) { return current.data == "value"; }, &decltype(properties)::value_type::first);
    if (maybeValue != properties.end())
    {
        assert(maybeValue->second->node_type() == Node::Type::EXPRESSION);
        assert(static_cast<Expression const*>(maybeValue->second.get())->expr_type() == Expression::Type::LITERAL);
        argumentStmt->value = std::move(maybeValue->second);
    }

    if (!argumentStmt->value)
    {
        auto value = TRY(parse_expression(tokens, cursor));

        if (value)
        {
            argumentStmt->value = std::move(value);
        }
        else
        {
            emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, {{ peek(tokens, cursor), "was found instead of 'value' property" }});
            return make_error({});
        }
    }

    TRY(parse_closing_tag(tokens, cursor, tag));

    return argumentStmt;
}

static Result<std::unique_ptr<Node>> parse_call(std::vector<Token> const& tokens, int& cursor)
{
    auto callStmt = std::make_unique<CallStmt>();

    auto [tag, properties] = TRY(parse_opening_tag(tokens, cursor, "call"));

    callStmt->token = tag;

    auto maybeWho = std::ranges::find_if(properties, [] (auto&& current) { return current.data == "who"; }, &decltype(properties)::value_type::first);
    if (maybeWho == properties.end())
    {
        emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, {{ tag, "requires property 'who'" }});
        return make_error({});
    }
    else
    {
        assert(maybeWho->second->node_type() == Node::Type::EXPRESSION);
        assert(static_cast<Expression const*>(maybeWho->second.get())->expr_type() == Expression::Type::LITERAL);
        callStmt->who = static_cast<LiteralExpr const*>(maybeWho->second.get())->value;
    }

    while (cursor > 0 && peek(tokens, cursor).depth > tag.depth)
    {
        auto argument = parse_arg(tokens, cursor);

        if (argument.has_value() && argument.value())
        {
            callStmt->arguments.push_back(std::move(argument.value()));
            continue;
        }

        if (!argument.has_value())
        {
            synchronize(tokens, tag, cursor);
            continue;
        }

        break;
    }

    TRY(parse_closing_tag(tokens, cursor, tag));

    return callStmt;
}

static Result<std::unique_ptr<Node>> parse_let(std::vector<Token> const& tokens, int& cursor)
{
    auto letStmt = std::make_unique<LetStmt>();

    auto [tag, properties] = TRY(parse_opening_tag(tokens, cursor, "let"));

    letStmt->token = tag;

    auto maybeName = std::ranges::find_if(properties, [] (auto&& current) { return current.data == "name"; }, &decltype(properties)::value_type::first);
    if (maybeName == properties.end())
    {
        emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, {{ tag, "requires property 'name'" }});
        return make_error({});
    }
    else
    {
        assert(maybeName->second->node_type() == Node::Type::EXPRESSION);
        assert(static_cast<Expression const*>(maybeName->second.get())->expr_type() == Expression::Type::LITERAL);
        letStmt->name = static_cast<LiteralExpr const*>(maybeName->second.get())->value;
    }

    auto maybeType = std::ranges::find_if(properties, [] (auto&& current) { return current.data == "type"; }, &decltype(properties)::value_type::first);
    if (maybeType == properties.end())
    {
        emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, {{ tag, "requires property 'type'" }});
        return make_error({});
    }
    else
    {
        assert(maybeType->second->node_type() == Node::Type::EXPRESSION);
        assert(static_cast<Expression const*>(maybeType->second.get())->expr_type() == Expression::Type::LITERAL);
        letStmt->type = static_cast<LiteralExpr const*>(maybeType->second.get())->value;
    }

    auto value = TRY(parse_expression(tokens, cursor));

    if (value)
    {
        letStmt->value = std::move(value);
    }
    else
    {
        emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, {{ peek(tokens, cursor), "was found instead of property 'value'" }});
        return make_error({});
    }

    TRY(parse_closing_tag(tokens, cursor, tag));

    return letStmt;
}

static Result<std::unique_ptr<Node>> parse_ret(std::vector<Token> const& tokens, int& cursor)
{
    auto returnStmt = std::make_unique<RetStmt>();

    auto [tag, properties] = TRY(parse_opening_tag(tokens, cursor, "return"));

    returnStmt->token = tag;

    auto maybeValue = std::ranges::find_if(properties, [] (auto&& current) { return current.data == "value"; }, &decltype(properties)::value_type::first);
    if (maybeValue != properties.end())
    {
        assert("UNIMPLEMENTED" && false);
    }

    if (!returnStmt->value)
    {
        auto value = TRY(parse_expression(tokens, cursor));

        if (value)
        {
            returnStmt->value = std::move(value);
        }
    }

    TRY(parse_closing_tag(tokens, cursor, tag));

    return returnStmt;
}

static Result<std::vector<std::unique_ptr<Node>>> parse_else(std::vector<Token> const& tokens, int& cursor);

static Result<std::unique_ptr<Node>> parse_if(std::vector<Token> const& tokens, int& cursor)
{
    auto ifStmt = std::make_unique<IfStmt>();

    auto [tag, properties] = TRY(parse_opening_tag(tokens, cursor, "if"));

    ifStmt->token = tag;

    auto maybeCondition = std::ranges::find_if(properties, [] (auto&& current) { return current.data == "condition"; }, &decltype(properties)::value_type::first);
    if (maybeCondition == properties.end())
    {
        emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, {{ tag, "requires property 'condition'" }});
        return make_error({});
    }
    else
    {
        assert("UNIMPLEMENTED" && false);
    }

    while (cursor > 0 && peek(tokens, cursor).depth > tag.depth)
    {
        auto node = parse_statement(tokens, cursor);

        if (node.has_value() && node.value())
        {
            ifStmt->trueBranch.push_back(std::move(node.value()));
            continue;
        }

        if (!node.has_value())
        {
            synchronize(tokens, tag, cursor);
            continue;
        }

        break;
    }

    TRY(parse_closing_tag(tokens, cursor, tag));

    if (peek(tokens, cursor, 1).type == Token::Type::KEYWORD && peek(tokens, cursor, 1).data == "else")
    {
        ifStmt->falseBranch = TRY(parse_else(tokens, cursor));
    }

    return ifStmt;
}

static Result<std::vector<std::unique_ptr<Node>>> parse_else(std::vector<Token> const& tokens, int& cursor)
{
    std::vector<std::unique_ptr<Node>> nodes {};

    auto [tag, _] = TRY(parse_opening_tag(tokens, cursor, "else"));

    while (cursor > 0 && peek(tokens, cursor).depth > tag.depth)
    {
        auto node = parse_statement(tokens, cursor);

        if (node.has_value() && node.value())
        {
            nodes.push_back(std::move(node.value()));
            continue;
        }

        if (!node.has_value())
        {
            synchronize(tokens, tag, cursor);
            continue;
        }

        break;
    }

    TRY(parse_closing_tag(tokens, cursor, tag));

    return {};
}

static Result<std::unique_ptr<Node>> parse_statement(std::vector<Token> const& tokens, int& cursor)
{
    if (peek(tokens, cursor, 1).data == "let") return parse_let(tokens, cursor);
    if (peek(tokens, cursor, 1).data == "call") return parse_call(tokens, cursor);
    if (peek(tokens, cursor, 1).data == "return") return parse_ret(tokens, cursor);
    if (peek(tokens, cursor, 1).data == "if") return parse_if(tokens, cursor);

    return {};
}

static Result<std::unique_ptr<Node>> parse_declaration(std::vector<Token> const& tokens, int& cursor);

static Result<std::unique_ptr<Node>> parse_function(std::vector<Token> const& tokens, int& cursor)
{
    auto functionDecl = std::make_unique<FunctionDecl>();

    auto [tag, properties] = TRY(parse_opening_tag(tokens, cursor, "function"));

    functionDecl->token = tag;

    auto maybeName = std::ranges::find_if(properties, [] (auto&& current) { return current.data == "name"; }, &decltype(properties)::value_type::first);
    if (maybeName == properties.end())
    {
        emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, {{ tag, "requires property 'name'" }});
        return make_error({});
    }
    else if (std::distance(properties.begin(), maybeName) != 0)
    {
        emit_parser_warning(ParserWarning::UNEXPECTED_TOKEN_POSITION, {{ maybeName->first, "should appear in first" }});
    }
    else
    {
        assert(maybeName->second->node_type() == Node::Type::EXPRESSION);
        assert(static_cast<Expression const*>(maybeName->second.get())->expr_type() == Expression::Type::LITERAL);
        functionDecl->name = static_cast<LiteralExpr const*>(maybeName->second.get())->value;
    }

    auto maybeType = std::ranges::find_if(properties, [] (auto&& current) { return current.data == "type"; }, &decltype(properties)::value_type::first);
    if (maybeType == properties.end())
    {
        emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, {{ tag, "requires property 'type'" }});
        return make_error({});
    }
    else if (std::distance(properties.begin(), maybeType) != 1)
    {
        emit_parser_warning(ParserWarning::UNEXPECTED_TOKEN_POSITION, {{ maybeType->first, "should appear in second" }});
    }
    else
    {
        assert(maybeType->second->node_type() == Node::Type::EXPRESSION);
        assert(static_cast<Expression const*>(maybeType->second.get())->expr_type() == Expression::Type::LITERAL);
        functionDecl->type = static_cast<LiteralExpr const*>(maybeType->second.get())->value;
    }

    for (auto const& [name, value] : properties | std::views::drop(2))
    {
        assert(value->node_type() == Node::Type::EXPRESSION);
        assert(static_cast<Expression const*>(value.get())->expr_type() == Expression::Type::LITERAL);
        functionDecl->parameters.push_back({ name.data, static_cast<LiteralExpr const*>(value.get())->value });
    }

    while (cursor > 0 && peek(tokens, cursor).depth > tag.depth)
    {
        auto statement = parse_statement(tokens, cursor);

        if (statement.has_value() && statement.value())
        {
            functionDecl->scope.push_back(std::move(statement.value()));
            continue;
        }

        if (!statement.has_value())
        {
            synchronize(tokens, tag, cursor);
            continue;
        }

        break;
    }

    auto maybeReturn = std::find_if(functionDecl->scope.begin(), functionDecl->scope.end(), [] (std::unique_ptr<Node> const& node) {
        return
            node->node_type() == Node::Type::STATEMENT &&
            static_cast<Statement*>(node.get())->stmt_type() == Statement::Type::RETURN;
    });

    if (maybeReturn == functionDecl->scope.end())
    {
        if (functionDecl->type == "none")
        {
            auto returnStmt = std::make_unique<RetStmt>();
            returnStmt->type = functionDecl->type;
            functionDecl->scope.push_back(std::move(returnStmt));
        }
        else
        {
            emit_parser_error(ParserError::MISSING_RETURN_STATEMENT, {{ tag, "expects a value to be returned, yet no <return> tag was found." }});
            return make_error({});
        }
    }
    else
    {
        static_cast<RetStmt*>(maybeReturn->get())->type = functionDecl->type;
    }

    TRY(parse_closing_tag(tokens, cursor, tag));

    return functionDecl;
}

static Result<std::unique_ptr<Node>> parse_declaration(std::vector<Token> const& tokens, int& cursor)
{
    if (peek(tokens, cursor, 1).data == "function") return TRY(parse_function(tokens, cursor));
    return {};
}

static Result<std::unique_ptr<Node>> parse_program(std::vector<Token> const& tokens, int& cursor)
{
    auto program = std::make_unique<ProgramDecl>();

    auto [tag, _] = TRY(parse_opening_tag(tokens, cursor, "program"));

    program->token = tag;

    while (cursor > 0 && peek(tokens, cursor).depth == tag.depth + 1)
    {
        Result<std::unique_ptr<Node>> node;

        if (is_next_declaration(tokens, cursor)) node = parse_declaration(tokens, cursor);
        else if (is_next_statement(tokens, cursor)) node = parse_statement(tokens, cursor);

        if (node.has_value() && node.value())
        {
            program->scope.push_back(std::move(node.value()));
            continue;
        }

        if (!node.has_value())
        {
            synchronize(tokens, tag, cursor);
            continue;
        }

        break;
    }

    auto maybeMain = std::find_if(program->scope.begin(), program->scope.end(), [] (std::unique_ptr<Node>& node) {
        return
            node->node_type() == Node::Type::DECLARATION &&
            static_cast<Declaration*>(node.get())->decl_type() == Declaration::Type::FUNCTION &&
            static_cast<FunctionDecl*>(node.get())->name == "main";
    });

    if (maybeMain != program->scope.end())
    {
        auto call = std::make_unique<CallStmt>();
        call->who = "main";
        program->scope.push_back(std::move(call));
    }

    TRY(parse_closing_tag(tokens, cursor, tag));

    return program;
}

Result<std::unique_ptr<Node>> parse(std::vector<Token> const& tokens)
{
    auto cursor  = static_cast<int>(tokens.size()-1);
    auto program = TRY(parse_program(tokens, cursor));

    if (hadAnError_g)
    {
        return make_error("I give up. ( ; ω ; )");
    }

    return program;
}

nlohmann::ordered_json dump_ast(std::unique_ptr<Node> const& node)
{
    assert(node);

    nlohmann::ordered_json ast;

    switch (node->node_type())
    {
        case Node::Type::STATEMENT: {
            auto statement = static_cast<Statement*>(node.get());

            switch (statement->stmt_type())
            {
            case Statement::Type::CALL: {
                auto callStmt = static_cast<CallStmt*>(statement);

                ast = {{
                    magic_enum::enum_name(callStmt->stmt_type()), {
                        { "who", callStmt->who },
                        { "arguments", nlohmann::json::array() }
                    }
                }};

                for (auto const& child : callStmt->arguments)
                {
                    ast[magic_enum::enum_name(callStmt->stmt_type())]["arguments"].push_back(dump_ast(child));
                }

                break;
            }
            case Statement::Type::ARG: {
                auto argumentStmt = static_cast<ArgStmt*>(statement);

                ast = {{
                    magic_enum::enum_name(argumentStmt->stmt_type()), {
                        { "value", dump_ast(argumentStmt->value) },
                    }
                }};

                break;
            }
            case Statement::Type::RETURN: {
                auto returnStmt = static_cast<RetStmt*>(statement);

                ast = {{
                    magic_enum::enum_name(returnStmt->stmt_type()), {
                        { "type", returnStmt->type },
                        { "value", returnStmt->value ? dump_ast(returnStmt->value) : "none" },
                    }
                }};

                break;
            }
            case Statement::Type::LET: {
                auto letStmt = static_cast<LetStmt*>(statement);

                ast = {{
                    magic_enum::enum_name(letStmt->stmt_type()), {
                        { "name", letStmt->name },
                        { "type", letStmt->type },
                        { "value", dump_ast(letStmt->value) },
                    }
                }};

                break;
            }
            case Statement::Type::IF: {
                auto ifStmt = static_cast<IfStmt*>(statement);

                ast = {{
                    magic_enum::enum_name(ifStmt->stmt_type()), {
                        { "condition", dump_ast(ifStmt->condition) },
                        { "trueBranch", nlohmann::json::array() },
                        { "falseBranch", nlohmann::json::array() },
                    }
                }};

                for (auto const& child : ifStmt->trueBranch)
                {
                    ast[magic_enum::enum_name(ifStmt->stmt_type())]["trueBranch"].push_back(dump_ast(child));
                }

                for (auto const& child : ifStmt->falseBranch)
                {
                    ast[magic_enum::enum_name(ifStmt->stmt_type())]["falseBranch"].push_back(dump_ast(child));
                }

                break;
            }
            }

            break;
        }
        case Node::Type::DECLARATION: {
            auto declaration = static_cast<Declaration*>(node.get());

            switch (declaration->decl_type())
            {
            case Declaration::Type::PROGRAM: {
                auto programDecl = static_cast<Declaration*>(declaration);

                ast = {{
                    magic_enum::enum_name(programDecl->decl_type()), {
                        { "scope", nlohmann::json::array() }
                    }
                }};

                for (auto const& child : programDecl->scope)
                {
                    ast[magic_enum::enum_name(programDecl->decl_type())]["scope"].push_back(dump_ast(child));
                }

                break;
            }
            case Declaration::Type::FUNCTION: {
                auto functionDecl = static_cast<FunctionDecl*>(declaration);

                ast = {{
                    magic_enum::enum_name(functionDecl->decl_type()), {
                        { "name", functionDecl->name },
                        { "type", functionDecl->type },
                        { "parameters", nlohmann::json::array() },
                        { "scope", nlohmann::json::array() }
                    }
                }};

                for (auto const& parameter : functionDecl->parameters)
                {
                    ast[magic_enum::enum_name(functionDecl->decl_type())]["parameters"].push_back({ { "name", parameter.first }, { "type", parameter.second } });
                }

                for (auto const& child : declaration->scope)
                {
                    ast[magic_enum::enum_name(functionDecl->decl_type())]["scope"].push_back(dump_ast(child));
                }

                break;
            }
            }

            break;
        }
        case Node::Type::EXPRESSION: {
            auto expression = static_cast<Expression*>(node.get());

            switch (expression->expr_type())
            {
            case Expression::Type::LITERAL: {
                auto literalExpr = static_cast<LiteralExpr*>(expression);

                ast = {{
                    magic_enum::enum_name(literalExpr->expr_type()), {
                        { "value", literalExpr->value }
                    }
                }};

                break;
            }
            case Expression::Type::ARITHMETIC: assert("UNIMPLEMENTED" && false);
            case Expression::Type::LOGICAL: assert("UNIMPLEMENTED" && false);
            }

            break;
        }
    }

    return ast;
}
