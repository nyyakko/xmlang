#include "Parser.hpp"

#include <libcoro/Generator.hpp>
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

// cppcheck-suppress [unknownMacro]
ENUM_CLASS(ParserError,
    UNEXPECTED_TOKEN_REACHED,
    EXPECTED_TOKEN_MISSING,
    ENCLOSING_TOKEN_MISSING,
    ENCLOSING_TOKEN_MISMATCH,
    UNEXPECTED_END_OF_FILE
)

auto hadAnError_g = false;

static Generator<std::string> next_line(std::filesystem::path const& path)
{
    std::ifstream stream(path);

    for (std::string line; std::getline(stream, line); )
    {
        co_yield line;
    }

    co_return;
}

static Generator<void> emit_parser_error(ParserError const& error, Token const& token, std::string const& message)
{
    hadAnError_g = true;

    std::vector<std::string> lines {};

    for (auto const& line : next_line(token.location.first))
    {
        lines.push_back(line);
    }

    switch (error)
    {
    case ParserError::UNEXPECTED_TOKEN_REACHED: {
        auto& [data, type, location, depth] = token;
        auto& [file, position] = location;
        auto& [line, column] = position;

        auto beforeToken = lines.at(line).substr(0, 1+column-data.size());
        auto afterToken  = column ? lines.at(line).substr(1+column) : "";

        std::cout << RED << "[error]" << RESET << ": unexpected token\n";
        std::cout << '\n';
        std::cout << "at " << file.string() << ':' << line+1 << ':' << beforeToken.size() + 1<< '\n';
        std::cout << '\n';
        std::cout << "    " << " | " << '\n';

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

        break;
    }
    case ParserError::EXPECTED_TOKEN_MISSING: {
        auto& [data, type, location, depth] = token;
        auto& [file, position] = location;
        auto& [line, column] = position;

        auto beforeToken = lines.at(line).substr(0, 1+column-data.size());
        auto afterToken  = column ? lines.at(line).substr(1+column) : "";

        std::cout << RED << "[error]" << RESET << ": missing expected token\n";
        std::cout << '\n';
        std::cout << "at " << file.string() << ':' << line+1 << ':' << beforeToken.size() + 1<< '\n';
        std::cout << '\n';
        std::cout << "    " << " | " << '\n';

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

        break;
    }
    case ParserError::ENCLOSING_TOKEN_MISSING: {
        auto& [data, type, location, depth] = token;
        auto& [file, position] = location;
        auto& [line, column] = position;

        auto beforeToken = lines.at(line).substr(0, 1+column-data.size());
        auto afterToken  = column ? lines.at(line).substr(1+column) : "";

        std::cout << RED << "[error]" << RESET << ": missing enclosing token\n";
        std::cout << '\n';
        std::cout << "at " << file.string() << ':' << line+1 << ':' << beforeToken.size() + 1 << '\n';
        std::cout << '\n';
        std::cout << "    " << " | " << '\n';

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

        break;
    }
    case ParserError::ENCLOSING_TOKEN_MISMATCH: {
        {
        auto& [data, type, location, depth] = token;
        auto& [file, position] = location;
        auto& [line, column] = position;

        auto beforeToken = lines.at(line).substr(0, 1+column-data.size());
        auto afterToken  = column ? lines.at(line).substr(1+column) : "";

        std::cout << RED << "[error]" << RESET << ": mismatching tokens found\n";
        std::cout << '\n';
        std::cout << "at " << file.string() << ':' << line+1 << ':' << beforeToken.size() + 1<< '\n';
        std::cout << '\n';
        std::cout << "    " << " | " << '\n';

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
        std::cout << "    " << " | " << std::string(beforeToken.size(), ' ') << RED << std::string(data.size(), '^') << RESET << ' ' << message << "\n\n";
        }

        co_yield {};

        {
        auto& [data, type, location, depth] = token;
        auto& [file, position] = location;
        auto& [line, column] = position;

        auto beforeToken = lines.at(line).substr(0, 1+column-data.size());
        auto afterToken  = column ? lines.at(line).substr(1+column) : "";

        std::cout << "at " << file.string() << ':' << line+1 << ':' << beforeToken.size() + 1<< '\n';
        std::cout << '\n';
        std::cout << "    " << " | " << '\n';

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

        break;
    }
    }

    std::cout << '\n';

    co_return;
}

// cppcheck-suppress [unknownMacro]
ENUM_CLASS(ParserWarning,
    UNEXPECTED_TOKEN_POSITION
)

static Generator<void> emit_parser_warning(ParserWarning const& warning, Token const& token, std::string const& message)
{
    std::vector<std::string> lines {};

    for (auto const& line : next_line(token.location.first))
    {
        lines.push_back(line);
    }

    switch (warning)
    {
    case ParserWarning::UNEXPECTED_TOKEN_POSITION: {
        auto& [data, type, location, depth] = token;
        auto& [file, position] = location;
        auto& [line, column] = position;

        auto beforeToken = lines.at(line).substr(0, 1+column-data.size());
        auto afterToken  = column ? lines.at(line).substr(1+column) : "";

        std::cout << YELLOW << "[warning]" << RESET << ": token in unexpected position\n";
        std::cout << '\n';
        std::cout << "at " << file.string() << ':' << line+1 << ':' << beforeToken.size() + 1<< '\n';
        std::cout << '\n';
        std::cout << "    " << " | " << '\n';

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

        break;
    }
    }

    std::cout << '\n';

    co_return;
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

static bool is_next_statement(std::vector<Token> const& tokens, int& cursor)
{
    return
        peek(tokens, cursor, 1).data == "let" ||
        peek(tokens, cursor, 1).data == "call" ||
        peek(tokens, cursor, 1).data == "arg" ||
        peek(tokens, cursor, 1).data == "new" ||
        peek(tokens, cursor, 1).data == "return"
        ;
}

static bool is_next_declaration(std::vector<Token> const& tokens, int& cursor)
{
    return
        peek(tokens, cursor, 1).data == "function" ||
        peek(tokens, cursor, 1).data == "class" ||
        peek(tokens, cursor, 1).data == "ctor" ||
        peek(tokens, cursor, 1).data == "dtor"
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

static Result<std::pair<Token, std::vector<std::pair<Token, Token>>>> parse_opening_tag(std::vector<Token> const& tokens, int& cursor, std::string_view name)
{
    if (!advance(tokens, cursor, Token::Type::LEFT_ANGLE))
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, peek(tokens, cursor), "was found instead of a '<'");
        return make_error({});
    }

    auto tag = advance(tokens, cursor, Token::Type::KEYWORD, name);

    if (!tag)
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, peek(tokens, cursor), "was found instead of a tag");
        return make_error({});
    }

    std::vector<std::pair<Token, Token>> properties {};

    while (cursor > 1 && peek(tokens, cursor).type != Token::Type::RIGHT_ANGLE)
    {
        auto parameterName = advance(tokens, cursor, Token::Type::IDENTIFIER);

        if (!parameterName)
        {
            emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, peek(tokens, cursor), "was found instead of a property name");
            return make_error({});
        }

        if (!advance(tokens, cursor, Token::Type::EQUAL))
        {
            emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, peek(tokens, cursor), "was found instead of equals");
            return make_error({});
        }

        if (!advance(tokens, cursor, Token::Type::QUOTE))
        {
            emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, peek(tokens, cursor), "was found instead of quotes");
            return make_error({});
        }

        auto parameterType = advance(tokens, cursor, Token::Type::LITERAL);

        if (!parameterType)
        {
            emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, peek(tokens, cursor), "was found instead of a property value");
            return make_error({});
        }

        if (!advance(tokens, cursor, Token::Type::QUOTE))
        {
            emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, peek(tokens, cursor), "was found instead of quotes");
            return make_error({});
        }

        properties.push_back({ *parameterName, *parameterType });
    }

    if (!advance(tokens, cursor, Token::Type::RIGHT_ANGLE))
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, peek(tokens, cursor), "was found instead of a '>'");
        return make_error({});
    }

    return std::make_pair(*tag , properties);
}

static Result<void> parse_closing_tag(std::vector<Token> const& tokens, int& cursor, Token const& tag)
{
    if (!advance(tokens, cursor, Token::Type::LEFT_ANGLE))
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, peek(tokens, cursor), "was found instead of a '<'");
        return make_error({});
    }

    if (!advance(tokens, cursor, Token::Type::SLASH))
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, peek(tokens, cursor), "was found instead of a '/'");
        return make_error({});
    }

    if (auto closing = advance(tokens, cursor, Token::Type::KEYWORD); !closing)
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, peek(tokens, cursor), "was found instead of a tag");
        return make_error({});
    }
    else if (closing->data != tag.data)
    {
        using namespace std::literals;

        auto token   = tag;
        auto message = "this tag"s;
        auto emitter = emit_parser_error(ParserError::ENCLOSING_TOKEN_MISMATCH, token, message);

        token   = *closing;
        message = "doesn't match with this one, so it cannot close.";
        emitter.next();

        return make_error({});
    }

    if (!advance(tokens, cursor, Token::Type::RIGHT_ANGLE))
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, peek(tokens, cursor), "was found instead of '>'");
        return make_error({});
    }

    return {};
}

static Result<std::unique_ptr<Node>> parse_expression(std::vector<Token> const& tokens, int& cursor)
{
    if (peek(tokens, cursor).type == Token::Type::LITERAL)
    {
        auto literalExpr = std::make_unique<LiteralExpr>();
        literalExpr->value = advance(tokens, cursor).data;
        return literalExpr;
    }

    if (peek(tokens, cursor, 1).type == Token::Type::KEYWORD)
    {
        assert("UNIMPLEMENTED" && false);
    }

    return {};
}

static Result<std::unique_ptr<Node>> parse_argument(std::vector<Token> const& tokens, int& cursor)
{
    auto argumentStmt = std::make_unique<ArgumentStmt>();

    auto [tag, properties] = TRY(parse_opening_tag(tokens, cursor, "arg"));

    auto maybeValue = std::ranges::find_if(properties, [] (auto&& current) { return current.data == "value"; }, &decltype(properties)::value_type::first);
    if (maybeValue != properties.end())
    {
        auto literal = std::make_unique<LiteralExpr>();
        literal->value = maybeValue->second.data;
        argumentStmt->value = std::move(literal);
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
            emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, peek(tokens, cursor), "was found instead of 'value' property");
            return make_error({});
        }
    }

    TRY(parse_closing_tag(tokens, cursor, tag));

    return argumentStmt;
}

static Result<std::unique_ptr<Node>> parse_statement(std::vector<Token> const& tokens, int& cursor);

static Result<std::unique_ptr<Node>> parse_call(std::vector<Token> const& tokens, int& cursor)
{
    auto callStmt = std::make_unique<CallStmt>();

    auto [tag, properties] = TRY(parse_opening_tag(tokens, cursor, "call"));

    callStmt->token = tag;

    auto maybeWho = std::ranges::find_if(properties, [] (auto&& current) { return current.data == "who"; }, &decltype(properties)::value_type::first);
    if (maybeWho == properties.end())
    {
        emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, tag, "requires property 'who'");
        return make_error({});
    }
    else
    {
        callStmt->who = maybeWho->second.data;
    }

    while (cursor > 0 && peek(tokens, cursor).depth > tag.depth)
    {
        auto argument = parse_argument(tokens, cursor);

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
        emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, tag, "requires property 'name'");
        return make_error({});
    }
    else
    {
        letStmt->name = maybeName->second.data;
    }

    auto maybeType = std::ranges::find_if(properties, [] (auto&& current) { return current.data == "type"; }, &decltype(properties)::value_type::first);
    if (maybeType == properties.end())
    {
        emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, tag, "requires property 'type'");
        return make_error({});
    }
    else
    {
        letStmt->type = maybeType->second.data;
    }

    auto maybeValue = std::ranges::find_if(properties, [] (auto&& current) { return current.data == "value"; }, &decltype(properties)::value_type::first);
    if (maybeValue != properties.end())
    {
        auto literal = std::make_unique<LiteralExpr>();
        literal->value = maybeValue->second.data;
        letStmt->value = std::move(literal);
    }

    if (!letStmt->value)
    {
        auto value = TRY(parse_expression(tokens, cursor));

        if (value)
        {
            letStmt->value = std::move(value);
        }
        else
        {
            emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, peek(tokens, cursor), "was found instead of property 'value'");
            return make_error({});
        }
    }

    TRY(parse_closing_tag(tokens, cursor, tag));

    return letStmt;
}

static Result<std::unique_ptr<Node>> parse_return(std::vector<Token> const& tokens, int& cursor)
{
    auto returnStmt = std::make_unique<ReturnStmt>();

    auto [tag, properties] = TRY(parse_opening_tag(tokens, cursor, "return"));

    returnStmt->token = tag;

    auto maybeValue = std::ranges::find_if(properties, [] (auto&& current) { return current.data == "value"; }, &decltype(properties)::value_type::first);
    if (maybeValue != properties.end())
    {
        auto literal = std::make_unique<LiteralExpr>();
        literal->value = maybeValue->second.data;
        returnStmt->value = std::move(literal);
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

static Result<std::unique_ptr<Node>> parse_statement(std::vector<Token> const& tokens, int& cursor)
{
    if (peek(tokens, cursor, 1).data == "let") return parse_let(tokens, cursor);
    if (peek(tokens, cursor, 1).data == "call") return parse_call(tokens, cursor);
    if (peek(tokens, cursor, 1).data == "return") return parse_return(tokens, cursor);

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
        emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, tag, "requires property 'name'");
        return make_error({});
    }
    else if (std::distance(properties.begin(), maybeName) != 0)
    {
        emit_parser_warning(ParserWarning::UNEXPECTED_TOKEN_POSITION, maybeName->first, "should appear in first");
    }
    else
    {
        functionDecl->name = maybeName->second.data;
    }

    auto maybeResult = std::ranges::find_if(properties, [] (auto&& current) { return current.data == "result"; }, &decltype(properties)::value_type::first);
    if (maybeResult == properties.end())
    {
        emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, tag, "requires property 'result'");
        return make_error({});
    }
    else if (std::distance(properties.begin(), maybeResult) != 1)
    {
        emit_parser_warning(ParserWarning::UNEXPECTED_TOKEN_POSITION, maybeResult->first, "should appear in second");
    }
    else
    {
        functionDecl->result = maybeResult->second.data;
    }

    for (auto const& [name, value] : properties | std::views::drop(2))
    {
        functionDecl->parameters.push_back({ name.data, value.data });
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
        auto returnStmt = std::make_unique<ReturnStmt>();
        functionDecl->scope.push_back(std::move(returnStmt));
    }

    TRY(parse_closing_tag(tokens, cursor, tag));

    return functionDecl;
}

static Result<std::unique_ptr<Node>> parse_ctor(std::vector<Token> const& tokens, int& cursor)
{
    auto ctorDecl = std::make_unique<FunctionDecl>();

    auto [tag, properties] = TRY(parse_opening_tag(tokens, cursor, "ctor"));

    ctorDecl->token = tag;
    ctorDecl->name = "ctor";

    while (cursor > 0 && peek(tokens, cursor).depth > tag.depth)
    {
        Result<std::unique_ptr<Node>> node;

        if (is_next_declaration(tokens, cursor)) node = parse_declaration(tokens, cursor);
        else if (is_next_statement(tokens, cursor)) node = parse_statement(tokens, cursor);

        if (node.has_value() && node.value())
        {
            ctorDecl->scope.push_back(std::move(node.value()));
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

    return ctorDecl;

}

static Result<std::unique_ptr<Node>> parse_dtor(std::vector<Token> const& tokens, int& cursor)
{
    auto dtorDecl = std::make_unique<FunctionDecl>();

    auto [tag, properties] = TRY(parse_opening_tag(tokens, cursor, "dtor"));

    dtorDecl->token = tag;
    dtorDecl->name = "dtor";

    while (cursor > 0 && peek(tokens, cursor).depth > tag.depth)
    {
        Result<std::unique_ptr<Node>> node;

        if (is_next_declaration(tokens, cursor)) node = parse_declaration(tokens, cursor);
        else if (is_next_statement(tokens, cursor)) node = parse_statement(tokens, cursor);

        if (node.has_value() && node.value())
        {
            dtorDecl->scope.push_back(std::move(node.value()));
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

    return dtorDecl;

}

static Result<std::unique_ptr<Node>> parse_class(std::vector<Token> const& tokens, int& cursor)
{
    auto classDecl = std::make_unique<ClassDecl>();

    auto [tag, properties] = TRY(parse_opening_tag(tokens, cursor, "class"));

    classDecl->token = tag;

    auto maybeName = std::ranges::find_if(properties, [] (auto&& current) { return current.data == "name"; }, &decltype(properties)::value_type::first);
    if (maybeName == properties.end())
    {
        Token expected {};
        expected.type = Token::Type::IDENTIFIER;
        expected.data = "name";
        emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, tag, "requires property 'name'");
        return make_error({});
    }
    else if (std::distance(properties.begin(), maybeName) != 0)
    {
        emit_parser_warning(ParserWarning::UNEXPECTED_TOKEN_POSITION, maybeName->first, "should appear in first");
    }
    else
    {
        classDecl->name = maybeName->second.data;
    }

    auto maybeInherits = std::ranges::find_if(properties, [] (auto&& current) { return current.data == "inherits"; }, &decltype(properties)::value_type::first);
    if (maybeInherits != properties.end())
    {
        std::stringstream stream(maybeInherits->second.data);
        std::string inherited;

        while (std::getline(stream, inherited, ','))
        {
            classDecl->inherits.push_back(inherited);
        }
    }
    else if (std::distance(properties.begin(), maybeInherits) != 1)
    {
        emit_parser_warning(ParserWarning::UNEXPECTED_TOKEN_POSITION, maybeInherits->first, "should appear in second");
    }

    while (cursor > 0 && peek(tokens, cursor).depth > tag.depth)
    {
        Result<std::unique_ptr<Node>> node;

        if (is_next_declaration(tokens, cursor)) node = parse_declaration(tokens, cursor);
        else if (is_next_statement(tokens, cursor)) node = parse_statement(tokens, cursor);

        if (node.has_value() && node.value())
        {
            classDecl->scope.push_back(std::move(node.value()));
            continue;
        }

        if (!node.has_value())
        {
            synchronize(tokens, tag, cursor);
            continue;
        }

        break;
    }

    auto maybeCtor = std::find_if(classDecl->scope.begin(), classDecl->scope.end(), [] (std::unique_ptr<Node> const& node) {
        return
            node->node_type() == Node::Type::DECLARATION &&
            static_cast<Declaration*>(node.get())->decl_type() == Declaration::Type::FUNCTION &&
            static_cast<FunctionDecl*>(node.get())->name == "ctor";
    });

    if (maybeCtor == classDecl->scope.end())
    {
        auto ctor = std::make_unique<FunctionDecl>();
        ctor->name = "ctor";
        ctor->result = "none";
        ctor->parameters.push_back(std::make_pair("self", classDecl->name));

        classDecl->scope.insert(classDecl->scope.begin(), std::move(ctor));
    }
    else
    {
        static_cast<FunctionDecl*>(maybeCtor->get())->parameters.push_back(std::make_pair("self", classDecl->name));
    }

    auto maybeDtor = std::find_if(classDecl->scope.begin(), classDecl->scope.end(), [] (std::unique_ptr<Node> const& node) {
        return
            node->node_type() == Node::Type::DECLARATION &&
            static_cast<Declaration*>(node.get())->decl_type() == Declaration::Type::FUNCTION &&
            static_cast<FunctionDecl*>(node.get())->name == "dtor";
    });

    if (maybeDtor == classDecl->scope.end())
    {
        auto dtor = std::make_unique<FunctionDecl>();
        dtor->name = "dtor";
        dtor->result = "none";
        dtor->parameters.push_back(std::make_pair("self", classDecl->name));

        classDecl->scope.insert(std::next(classDecl->scope.begin()), std::move(dtor));
    }
    else
    {
        static_cast<FunctionDecl*>(maybeDtor->get())->parameters.push_back(std::make_pair("self", classDecl->name));
    }

    TRY(parse_closing_tag(tokens, cursor, tag));

    return classDecl;
}

static Result<std::unique_ptr<Node>> parse_declaration(std::vector<Token> const& tokens, int& cursor)
{
    if (peek(tokens, cursor, 1).data == "function") return TRY(parse_function(tokens, cursor));
    if (peek(tokens, cursor, 1).data == "class") return TRY(parse_class(tokens, cursor));
    if (peek(tokens, cursor, 1).data == "ctor") return TRY(parse_ctor(tokens, cursor));
    if (peek(tokens, cursor, 1).data == "dtor") return TRY(parse_dtor(tokens, cursor));

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
                auto program = static_cast<CallStmt*>(statement);

                ast = {{
                    program->stmt_type(), {
                        { "who", program->who },
                        { "arguments", nlohmann::json::array() }
                    }
                }};

                for (auto const& child : program->arguments)
                {
                    ast[program->stmt_type().to_string()]["arguments"].push_back(dump_ast(child));
                }

                break;
            }
            case Statement::Type::ARGUMENT: {
                auto argument = static_cast<ArgumentStmt*>(statement);

                ast = {{
                    argument->stmt_type(), {
                        { "value", dump_ast(argument->value) },
                    }
                }};

                break;
            }
            case Statement::Type::RETURN: {
                auto ret = static_cast<ReturnStmt*>(statement);

                ast = {{
                    ret->stmt_type(), {
                        { "value", ret->value ? dump_ast(ret->value) : "none" },
                    }
                }};

                break;
            }
            case Statement::Type::LET: {
                auto let = static_cast<LetStmt*>(statement);

                ast = {{
                    let->stmt_type(), {
                        { "name", let->name },
                        { "type", let->type },
                        { "value", dump_ast(let->value) },
                    }
                }};

                break;
            }
            case Statement::Type::EXPRESSION: {
                auto expression = static_cast<Expression*>(statement);

                switch (expression->expr_type())
                {
                case Expression::Type::LITERAL: {
                    auto literal = static_cast<LiteralExpr*>(expression);
                    ast = {{
                        literal->expr_type(), {
                            { "value", literal->value },
                        }
                    }};
                    break;
                }
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
                auto program = static_cast<Declaration*>(declaration);

                ast = {{
                    program->decl_type(), {
                        { "scope", nlohmann::json::array() }
                    }
                }};

                for (auto const& child : program->scope)
                {
                    ast[program->decl_type().to_string()]["scope"].push_back(dump_ast(child));
                }

                break;
            }
            case Declaration::Type::FUNCTION: {
                auto function = static_cast<FunctionDecl*>(declaration);

                ast = {{
                    function->decl_type(), {
                        { "name", function->name },
                        { "result", function->result },
                        { "parameters", nlohmann::json::array() },
                        { "scope", nlohmann::json::array() }
                    }
                }};

                for (auto const& parameter : function->parameters)
                {
                    ast[function->decl_type().to_string()]["parameters"].push_back({ { "name", parameter.first }, { "type", parameter.second } });
                }

                for (auto const& child : declaration->scope)
                {
                    ast[function->decl_type().to_string()]["scope"].push_back(dump_ast(child));
                }

                break;
            }
            case Declaration::Type::CLASS: {
                auto clazz = static_cast<ClassDecl*>(declaration);

                ast = {{
                    clazz->decl_type(), {
                        { "name", clazz->name },
                        { "inherits", nlohmann::json::array() },
                        { "scope", nlohmann::json::array() }
                    }
                }};

                for (auto const& parameter : clazz->inherits)
                {
                    ast[clazz->decl_type().to_string()]["inherits"].push_back(parameter);
                }

                for (auto const& child : declaration->scope)
                {
                    ast[clazz->decl_type().to_string()]["scope"].push_back(dump_ast(child));
                }

                break;
            }
            }

            break;
        }
    }

    return ast;
}
