#include <argparse/argparse.hpp>
#include <fmt/format.h>
#include <libcoro/Generator.hpp>
#include <libenum/Enum.hpp>
#include <liberror/Result.hpp>
#include <liberror/Try.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <vector>
#include <ranges>

using namespace libcoro;
using namespace liberror;
using namespace std::literals;

using Location = std::pair<std::filesystem::path, std::pair<size_t, size_t>>;

struct Token
{
    // cppcheck-suppress [unknownMacro]
    ENUM_CLASS(Type,
        LEFT_ANGLE,
        RIGHT_ANGLE,
        QUOTE,
        SLASH,
        EQUAL,
        KEYWORD,
        LITERAL,
        IDENTIFIER,
        END_OF_FILE
    )

    std::string data;
    Type type;
    Location location;
    size_t depth;
};

bool operator==(Token const& lhs, Token const& rhs)
{
    return
        lhs.location.first         == rhs.location.first &&
        lhs.location.second.first  == rhs.location.second.first &&
        lhs.location.second.second == rhs.location.second.second
    ;
}

Generator<std::pair<std::string, size_t>> next_file_line(std::filesystem::path const& path)
{
    std::ifstream stream(path);

    size_t number = 0;

    for (std::string line; std::getline(stream, line); )
    {
        co_yield std::make_pair(line, number);
        number += 1;
    }

    co_return;
}

static constexpr std::array KEYWORDS {
    "arg",
    "call",
    "function",
    "let",
    "program",
    "return",
    "class",
    "new",
    "ctor",
    "dtor",
};

Generator<Token> next_token(std::string_view line)
{
    size_t depth = 0;

    for (auto cursor = 0zu; cursor < line.size(); cursor += 1)
    {
        size_t space = 0;

        while (cursor != line.size() && line.at(cursor) == ' ')
        {
            space += 1;
            cursor += 1;
        }

        if (space % 4 == 0)
        {
            depth += space / 4;
        }

        if (line.at(cursor) == '<')
        {
            co_yield Token { .data = { line.at(cursor) }, .type = Token::Type::LEFT_ANGLE, .location = { {}, { {}, cursor } }, .depth = depth };
        }
        else if (line.at(cursor) == '>')
        {
            co_yield Token { .data = { line.at(cursor) }, .type = Token::Type::RIGHT_ANGLE, .location = { {}, { {}, cursor } }, .depth = depth  };

            if (!(cursor < line.size() && (std::isalpha(line.at(cursor+1)) || std::isdigit(line.at(cursor+1)))))
            {
                continue;
            }

            cursor++;

            std::string data;

            while (true)
            {
                data += line.at(cursor);

                if (cursor+1 >= line.size()) break;

                if (line.at(cursor+1) == '<' || line.at(cursor+1) == '>')
                {
                    break;
                }

                cursor++;
            }

            if (!data.empty())
            {
                co_yield Token { .data = data, .type = Token::Type::LITERAL, .location = { {}, { {}, cursor } }, .depth = depth  };
            }
        }
        else if (line.at(cursor) == '/')
        {
            co_yield Token { .data = { line.at(cursor) }, .type = Token::Type::SLASH, .location = { {}, { {}, cursor } }, .depth = depth  };
        }
        else if (line.at(cursor) == '=')
        {
            co_yield Token { .data = { line.at(cursor) }, .type = Token::Type::EQUAL, .location = { {}, { {}, cursor } }, .depth = depth  };
        }
        else if (line.at(cursor) == '"')
        {
            co_yield Token { .data = { line.at(cursor) }, .type = Token::Type::QUOTE, .location = { {}, { {}, cursor } }, .depth = depth  };

            if (!(cursor < line.size() && (std::isalpha(line.at(cursor+1)) || line.at(cursor+1) == '$' || line.at(cursor+1) == '{' || line.at(cursor+1) == '}')))
            {
                continue;
            }

            cursor++;

            std::string data;

            while (true)
            {
                data += line.at(cursor);

                if (cursor+1 >= line.size()) break;

                if (line.at(cursor+1) == '"')
                {
                    break;
                }

                cursor++;
            }

            co_yield Token { .data = data, .type = Token::Type::LITERAL, .location = { {}, { {}, cursor } }, .depth = depth  };
        }
        else
        {
            std::string data;

            while (true)
            {
                data += line.at(cursor);

                if (cursor+1 >= line.size()) break;

                if (line.at(cursor+1) == ' ' || line.at(cursor+1) == '=' ||
                    line.at(cursor+1) == '<' || line.at(cursor+1) == '>' ||
                    line.at(cursor+1) == '"')
                {
                    break;
                }

                cursor++;
            }

            if (std::find(KEYWORDS.begin(), KEYWORDS.end(), data) != KEYWORDS.end())
            {
                co_yield Token { .data = data, .type = Token::Type::KEYWORD, .location = { {}, { {}, cursor } }, .depth = depth };
            }
            else
            {
                co_yield Token { .data = data, .type = Token::Type::IDENTIFIER, .location = { {}, { {}, cursor } }, .depth = depth };
            }
        }
    }

    co_return;
}

std::vector<Token> tokenize(std::filesystem::path const& path)
{
    std::vector<Token> tokens {};

    size_t lineCount = 0;

    for (auto const& [line, number] : next_file_line(path))
    {
        for (auto token : next_token(line))
        {
            token.location.first = path;
            token.location.second.first = number;
            tokens.push_back(token);
        }

        lineCount += 1;
    }

    tokens.push_back({
        .data = "EOF",
        .type = Token::Type::END_OF_FILE,
        .location = { path, { lineCount-1, 0 } },
        .depth = 0
    });

    std::reverse(tokens.begin(), tokens.end());

    return tokens;
}

auto dump_tokens(std::vector<Token> const& tokens)
{
    nlohmann::ordered_json result {};

    for (auto const& token : tokens)
    {
        nlohmann::ordered_json json {
            { "data", token.data },
            { "type", token.type.to_string() },
            {
                "location", {
                    { "file", token.location.first },
                    { "line", token.location.second.first },
                    { "column", token.location.second.second }
                }
            },
            { "depth", token.depth }
        };

        result.push_back(json);
    }

    return result;
}

#define NODE_TYPE(TYPE)                                                  \
constexpr virtual Node::Type node_type() const override { return TYPE; } \

struct Node
{
    // cppcheck-suppress [unknownMacro]
    ENUM_CLASS(Type, STATEMENT, DECLARATION)

    virtual ~Node() = default;

    constexpr virtual Type node_type() const = 0;

    Token token;
};

struct Declaration : public Node
{
    NODE_TYPE(Node::Type::DECLARATION);

    // cppcheck-suppress [unknownMacro]
    ENUM_CLASS(Type, FUNCTION, PROGRAM, CLASS)

    constexpr virtual Type decl_type() const = 0;

    std::vector<std::unique_ptr<Node>> scope;
};

#define DECL_TYPE(TYPE)                                                         \
constexpr virtual Declaration::Type decl_type() const override { return TYPE; } \

struct ProgramDecl : public Declaration
{
    DECL_TYPE(Declaration::Type::PROGRAM);
};

struct ClassDecl : public Declaration
{
    DECL_TYPE(Declaration::Type::CLASS);

    std::string name {};
    std::vector<std::string> inherits {};
};

struct FunctionDecl : public Declaration
{
    DECL_TYPE(Declaration::Type::FUNCTION);

    std::string result {};
    std::vector<std::pair<std::string, std::string>> parameters {};
    std::string name {};
};

#define STMT_TYPE(TYPE)                                                       \
constexpr virtual Statement::Type stmt_type() const override { return TYPE; } \

struct Statement : public Node
{
    NODE_TYPE(Node::Type::STATEMENT);

    // cppcheck-suppress [unknownMacro]
    ENUM_CLASS(Type,
        ARGUMENT,
        CALL,
        EXPRESSION,
        LET,
        RETURN,
    )

    constexpr virtual Type stmt_type() const = 0;
};

struct CallStmt : public Statement
{
    STMT_TYPE(Statement::Type::CALL);

    std::vector<std::unique_ptr<Node>> arguments {};
    std::string who {};
};

struct ArgumentStmt : public Statement
{
    STMT_TYPE(Statement::Type::ARGUMENT);

    std::unique_ptr<Node> value {};
};

struct ReturnStmt : public Statement
{
    STMT_TYPE(Statement::Type::RETURN);

    std::unique_ptr<Node> value {};
};

struct LetStmt : public Statement
{
    STMT_TYPE(Statement::Type::LET);

    std::string name;
    std::string type;
    std::unique_ptr<Node> value {};
};

#define EXPR_TYPE(TYPE)                                                        \
constexpr virtual Expression::Type expr_type() const override { return TYPE; } \

struct Expression : public Statement
{
    STMT_TYPE(Statement::Type::EXPRESSION);

    ENUM_CLASS(Type,
        LITERAL,
    )

    constexpr virtual Type expr_type() const = 0;
};

struct LiteralExpr : public Expression
{
    EXPR_TYPE(Expression::Type::LITERAL)

    std::string value;
};

auto static constexpr RED = "\033[31m";
auto static constexpr GREEN = "\033[32m";
auto static constexpr BLUE = "\033[34m";
auto static constexpr YELLOW = "\033[33m";
auto static constexpr RESET = "\033[00m";

ENUM_CLASS(ParserError,
    UNEXPECTED_TOKEN_REACHED,
    EXPECTED_TOKEN_MISSING,
    ENCLOSING_TOKEN_MISSING,
    ENCLOSING_TOKEN_MISMATCH,
    UNEXPECTED_END_OF_FILE
)

bool hadAnError_g = false;

Generator<void> emit_parser_error(ParserError const& error, Token const& token, std::string const& message)
{
    hadAnError_g = true;

    std::vector<std::string> lines {};

    for (auto const& line : next_file_line(token.location.first))
    {
        lines.push_back(line.first);
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

ENUM_CLASS(ParserWarning,
    UNEXPECTED_TOKEN_POSITION
)

Generator<void> emit_parser_warning(ParserWarning const& warning, Token const& token, std::string const& message)
{
    std::vector<std::string> lines {};

    for (auto const& line : next_file_line(token.location.first))
    {
        lines.push_back(line.first);
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

bool expect(std::vector<Token> const& tokens, int cursor, Token::Type type)
{
    if (cursor == -1 || tokens.at(static_cast<size_t>(cursor)).type != type)
    {
        return false;
    }

    return true;
}

bool expect(std::vector<Token> const& tokens, int cursor, Token::Type type, std::string_view data)
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

Token peek_cursor(std::vector<Token> const& tokens, int cursor, int distance = 0)
{
    return tokens.at(static_cast<size_t>(cursor - distance));
}

Token advance_cursor(std::vector<Token> const& tokens, int& cursor)
{
    return tokens.at(static_cast<size_t>(cursor--));
}

Result<Token> advance_if_present(std::vector<Token> const& tokens, int& cursor, Token::Type type)
{
    if (expect(tokens, cursor, type))
    {
        return tokens.at(static_cast<size_t>(cursor--));
    }

    return make_error({});
}

Result<Token> advance_if_present(std::vector<Token> const& tokens, int& cursor, Token::Type type, std::string_view data)
{
    if (expect(tokens, cursor, type, data))
    {
        return tokens.at(static_cast<size_t>(cursor--));
    }

    return make_error({});
}

bool next_is_statement(std::vector<Token> const& tokens, int& cursor)
{
    return
        peek_cursor(tokens, cursor, 1).data == "let" ||
        peek_cursor(tokens, cursor, 1).data == "call" ||
        peek_cursor(tokens, cursor, 1).data == "arg" ||
        peek_cursor(tokens, cursor, 1).data == "new" ||
        peek_cursor(tokens, cursor, 1).data == "return"
        ;
}

bool next_is_declaration(std::vector<Token> const& tokens, int& cursor)
{
    return
        peek_cursor(tokens, cursor, 1).data == "function" ||
        peek_cursor(tokens, cursor, 1).data == "class" ||
        peek_cursor(tokens, cursor, 1).data == "ctor" ||
        peek_cursor(tokens, cursor, 1).data == "dtor"
        ;
}

void synchronize(std::vector<Token> const& tokens, Token const& token, int& cursor)
{
    while (
        cursor > 2 &&
        !(
            (peek_cursor(tokens, cursor, 0).type == Token::Type::LEFT_ANGLE && peek_cursor(tokens, cursor, 1).type == Token::Type::KEYWORD &&
             peek_cursor(tokens, cursor, 0).depth == token.depth + 1) ||
             peek_cursor(tokens, cursor, 0).depth == token.depth
        )
    )
    {
        advance_cursor(tokens, cursor);
    }
}

Result<std::pair<Token, std::vector<std::pair<Token, Token>>>> parse_opening_tag(std::vector<Token> const& tokens, int& cursor, std::string_view name)
{
    if (!advance_if_present(tokens, cursor, Token::Type::LEFT_ANGLE))
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, peek_cursor(tokens, cursor), "was found instead of a '<'");
        return make_error({});
    }

    auto tag = advance_if_present(tokens, cursor, Token::Type::KEYWORD, name);

    if (!tag)
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, peek_cursor(tokens, cursor), "was found instead of a tag");
        return make_error({});
    }

    std::vector<std::pair<Token, Token>> properties {};

    while (cursor > 1 && peek_cursor(tokens, cursor).type != Token::Type::RIGHT_ANGLE)
    {
        auto parameterName = advance_if_present(tokens, cursor, Token::Type::IDENTIFIER);

        if (!parameterName)
        {
            emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, peek_cursor(tokens, cursor), "was found instead of a property name");
            return make_error({});
        }

        if (!advance_if_present(tokens, cursor, Token::Type::EQUAL))
        {
            emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, peek_cursor(tokens, cursor), "was found instead of equals");
            return make_error({});
        }

        if (!advance_if_present(tokens, cursor, Token::Type::QUOTE))
        {
            emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, peek_cursor(tokens, cursor), "was found instead of quotes");
            return make_error({});
        }

        auto parameterType = advance_if_present(tokens, cursor, Token::Type::LITERAL);

        if (!parameterType)
        {
            emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, peek_cursor(tokens, cursor), "was found instead of a property value");
            return make_error({});
        }

        if (!advance_if_present(tokens, cursor, Token::Type::QUOTE))
        {
            emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, peek_cursor(tokens, cursor), "was found instead of quotes");
            return make_error({});
        }

        properties.push_back({ *parameterName, *parameterType });
    }

    if (!advance_if_present(tokens, cursor, Token::Type::RIGHT_ANGLE))
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, peek_cursor(tokens, cursor), "was found instead of a '>'");
        return make_error({});
    }

    return std::make_pair(*tag , properties);
}

Result<void> parse_closing_tag(std::vector<Token> const& tokens, int& cursor, Token const& tag)
{
    if (!advance_if_present(tokens, cursor, Token::Type::LEFT_ANGLE))
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, peek_cursor(tokens, cursor), "was found instead of a '<'");
        return make_error({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::SLASH))
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, peek_cursor(tokens, cursor), "was found instead of a '/'");
        return make_error({});
    }

    if (auto closing = advance_if_present(tokens, cursor, Token::Type::KEYWORD); !closing)
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, peek_cursor(tokens, cursor), "was found instead of a tag");
        return make_error({});
    }
    else if (closing->data != tag.data)
    {
        auto token   = tag;
        auto message = "this tag"s;
        auto emitter = emit_parser_error(ParserError::ENCLOSING_TOKEN_MISMATCH, token, message);

        token   = *closing;
        message = "doesn't match with this one, so it cannot close.";
        emitter.next();

        return make_error({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::RIGHT_ANGLE))
    {
        emit_parser_error(ParserError::UNEXPECTED_TOKEN_REACHED, peek_cursor(tokens, cursor), "was found instead of '>'");
        return make_error({});
    }

    return {};
}

Result<std::unique_ptr<Node>> parse_expression(std::vector<Token> const& tokens, int& cursor)
{
    if (peek_cursor(tokens, cursor).type == Token::Type::LITERAL)
    {
        auto literalExpr = std::make_unique<LiteralExpr>();
        literalExpr->value = advance_cursor(tokens, cursor).data;
        return literalExpr;
    }

    if (peek_cursor(tokens, cursor, 1).type == Token::Type::KEYWORD)
    {
        assert("UNIMPLEMENTED" && false);
    }

    return {};
}

Result<std::unique_ptr<Node>> parse_argument(std::vector<Token> const& tokens, int& cursor)
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
            emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, peek_cursor(tokens, cursor), "was found instead of 'value' property");
            return make_error({});
        }
    }

    TRY(parse_closing_tag(tokens, cursor, tag));

    return argumentStmt;
}

Result<std::unique_ptr<Node>> parse_statement(std::vector<Token> const& tokens, int& cursor);

Result<std::unique_ptr<Node>> parse_call(std::vector<Token> const& tokens, int& cursor)
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

    while (cursor > 0 && peek_cursor(tokens, cursor).depth > tag.depth)
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

Result<std::unique_ptr<Node>> parse_let(std::vector<Token> const& tokens, int& cursor)
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
            emit_parser_error(ParserError::EXPECTED_TOKEN_MISSING, peek_cursor(tokens, cursor), "was found instead of property 'value'");
            return make_error({});
        }
    }

    TRY(parse_closing_tag(tokens, cursor, tag));

    return letStmt;
}

Result<std::unique_ptr<Node>> parse_return(std::vector<Token> const& tokens, int& cursor)
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

Result<std::unique_ptr<Node>> parse_statement(std::vector<Token> const& tokens, int& cursor)
{
    if (peek_cursor(tokens, cursor, 1).data == "let") return parse_let(tokens, cursor);
    if (peek_cursor(tokens, cursor, 1).data == "call") return parse_call(tokens, cursor);
    if (peek_cursor(tokens, cursor, 1).data == "return") return parse_return(tokens, cursor);

    return {};
}

Result<std::unique_ptr<Node>> parse_declaration(std::vector<Token> const& tokens, int& cursor);

Result<std::unique_ptr<Node>> parse_function(std::vector<Token> const& tokens, int& cursor)
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

    while (cursor > 0 && peek_cursor(tokens, cursor).depth > tag.depth)
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

    TRY(parse_closing_tag(tokens, cursor, tag));

    return functionDecl;
}

Result<std::unique_ptr<Node>> parse_ctor(std::vector<Token> const& tokens, int& cursor)
{
    auto ctorDecl = std::make_unique<FunctionDecl>();

    auto [tag, properties] = TRY(parse_opening_tag(tokens, cursor, "ctor"));

    ctorDecl->token = tag;
    ctorDecl->name = "ctor";

    while (cursor > 0 && peek_cursor(tokens, cursor).depth > tag.depth)
    {
        Result<std::unique_ptr<Node>> node;

        if (next_is_declaration(tokens, cursor)) node = parse_declaration(tokens, cursor);
        else if (next_is_statement(tokens, cursor)) node = parse_statement(tokens, cursor);

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

Result<std::unique_ptr<Node>> parse_dtor(std::vector<Token> const& tokens, int& cursor)
{
    auto dtorDecl = std::make_unique<FunctionDecl>();

    auto [tag, properties] = TRY(parse_opening_tag(tokens, cursor, "dtor"));

    dtorDecl->token = tag;
    dtorDecl->name = "dtor";

    while (cursor > 0 && peek_cursor(tokens, cursor).depth > tag.depth)
    {
        Result<std::unique_ptr<Node>> node;

        if (next_is_declaration(tokens, cursor)) node = parse_declaration(tokens, cursor);
        else if (next_is_statement(tokens, cursor)) node = parse_statement(tokens, cursor);

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

Result<std::unique_ptr<Node>> parse_class(std::vector<Token> const& tokens, int& cursor)
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

    while (cursor > 0 && peek_cursor(tokens, cursor).depth > tag.depth)
    {
        Result<std::unique_ptr<Node>> node;

        if (next_is_declaration(tokens, cursor)) node = parse_declaration(tokens, cursor);
        else if (next_is_statement(tokens, cursor)) node = parse_statement(tokens, cursor);

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

Result<std::unique_ptr<Node>> parse_declaration(std::vector<Token> const& tokens, int& cursor)
{
    if (peek_cursor(tokens, cursor, 1).data == "function") return TRY(parse_function(tokens, cursor));
    if (peek_cursor(tokens, cursor, 1).data == "class") return TRY(parse_class(tokens, cursor));
    if (peek_cursor(tokens, cursor, 1).data == "ctor") return TRY(parse_ctor(tokens, cursor));
    if (peek_cursor(tokens, cursor, 1).data == "dtor") return TRY(parse_dtor(tokens, cursor));

    return {};
}

Result<std::unique_ptr<Node>> parse_program(std::vector<Token> const& tokens, int& cursor)
{
    auto program = std::make_unique<ProgramDecl>();

    auto [tag, _] = TRY(parse_opening_tag(tokens, cursor, "program"));

    program->token = tag;

    while (cursor > 0 && peek_cursor(tokens, cursor).depth == tag.depth + 1)
    {
        Result<std::unique_ptr<Node>> node;

        if (next_is_declaration(tokens, cursor)) node = parse_declaration(tokens, cursor);
        else if (next_is_statement(tokens, cursor)) node = parse_statement(tokens, cursor);

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

ENUM_CLASS(CompilerError,
    MISMATCHING_ARGUMENT_COUNT,
    MISMATCHING_ARGUMENT_TYPE
)

Generator<void> emit_compiler_error(CompilerError const& error, Token const& token, std::string const& message)
{
    hadAnError_g = true;

    std::vector<std::string> lines {};

    for (auto const& line : next_file_line(token.location.first))
    {
        lines.push_back(line.first);
    }

    switch (error)
    {
    case CompilerError::MISMATCHING_ARGUMENT_COUNT: {
        {
        auto& [data, type, location, depth] = token;
        auto& [file, position] = location;
        auto& [line, column] = position;

        auto beforeToken = lines.at(line).substr(0, 1+column-data.size());
        auto afterToken  = column ? lines.at(line).substr(1+column) : "";

        std::cout << RED << "[error]" << RESET << ": mismatching argument count\n";
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
        }
        co_yield {};
        {
        auto& [data, type, location, depth] = token;
        auto& [file, position] = location;
        auto& [line, column] = position;

        auto beforeToken = lines.at(line).substr(0, 1+column-data.size());
        auto afterToken  = column ? lines.at(line).substr(1+column) : "";

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
        }

        break;
    }
    case CompilerError::MISMATCHING_ARGUMENT_TYPE: {
        {
        auto& [data, type, location, depth] = token;
        auto& [file, position] = location;
        auto& [line, column] = position;

        auto beforeToken = lines.at(line).substr(0, 1+column-data.size());
        auto afterToken  = column ? lines.at(line).substr(1+column) : "";

        std::cout << RED << "[error]" << RESET << ": mismatching argument type\n";
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
        }
        co_yield {};
        {
        auto& [data, type, location, depth] = token;
        auto& [file, position] = location;
        auto& [line, column] = position;

        auto beforeToken = lines.at(line).substr(0, 1+column-data.size());
        auto afterToken  = column ? lines.at(line).substr(1+column) : "";

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
        }

        break;
    }
    }

    co_return;
}

Generator<void> emit_compiler_warning(CompilerError const& error, Token const& token, std::string const& message)
{
    (void)error;
    (void)token;
    (void)message;
    assert("UNIMPLEMENTED" && false);
    co_return;
}

Result<std::string> compile_return(ReturnStmt*)
{
    return "0x05";
}

Result<std::string> compile_let(LetStmt* letStmt)
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

Result<std::string> compile_argument(std::vector<LetStmt*> const& variables, ArgumentStmt* argumentStmt)
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

Result<std::string> compile_call(std::vector<LetStmt*> const& variables, CallStmt* callStmt)
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

Result<std::string> compile_function(FunctionDecl* functionDecl)
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

Result<std::string> compile_program(ProgramDecl* programDecl)
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

Result<std::vector<uint32_t>> compile(std::unique_ptr<Node> const& ast)
{
    std::vector<uint32_t> program {};

    std::stringstream stream(TRY(compile_program(static_cast<ProgramDecl*>(ast.get()))));

    for (uint32_t instruction; stream >> std::hex >> instruction; )
    {
        program.push_back(instruction);
    }

    return program;
}

Result<void> safe_main(std::span<char const*> arguments)
{
    argparse::ArgumentParser args("xmlang", "", argparse::default_arguments::help);
    args.add_description("xmlang compiler");

    args.add_argument("-f", "--file").help("file to be compiled").required();
    args.add_argument("-d", "--dump").choices("ast", "tokens").help("dumps the given xmlang script");
    args.add_argument("-o", "--output").help("name of the generated byetcode file");

    try
    {
        args.parse_args(static_cast<int>(arguments.size()), arguments.data());
    }
    catch (std::exception const& exception)
    {
        return liberror::make_error(exception.what());
    }

    auto source = args.get<std::string>("--file");

    if (!std::filesystem::exists(source))
    {
        return make_error("source {} does not exist.", source);
    }

    auto tokens = tokenize(source);

    if (args.has_value("--dump") && args.get<std::string>("--dump") == "tokens")
    {
        std::cout << std::setw(4) << dump_tokens(tokens);
        return {};
    }

    auto ast = TRY(parse(tokens));

    if (args.has_value("--dump") && args.get<std::string>("--dump") == "ast")
    {
        std::cout << std::setw(4) << dump_ast(ast) << '\n';
        return {};
    }

    std::string filename = "out.lmx";

    if (args.has_value("--output"))
    {
        filename = fmt::format("{}.lmx", args.get<std::string>("--output"));
    }

    std::ofstream stream(filename, std::ios::binary);

    for (auto instruction : TRY(compile(ast)))
    {
        stream.write(reinterpret_cast<char const*>(&instruction), sizeof(uint8_t));
    }

    return {};
}

int main(int argc, char const** argv)
{
    auto result = safe_main(std::span<char const*>(argv, size_t(argc)));

    if (!result.has_value())
    {
        std::cout << result.error().message() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
