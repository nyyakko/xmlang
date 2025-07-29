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
#include <vector>

using namespace libcoro;
using namespace liberror;

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
    "return"
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

            if (!(cursor < line.size() && std::isalpha(line.at(cursor+1))))
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
};

struct Declaration : public Node
{
    NODE_TYPE(Node::Type::DECLARATION);

    // cppcheck-suppress [unknownMacro]
    ENUM_CLASS(Type, FUNCTION, PROGRAM)

    constexpr virtual Type decl_type() const = 0;

    std::vector<std::unique_ptr<Node>> scope;
};

#define DECL_TYPE(TYPE)                                                         \
constexpr virtual Declaration::Type decl_type() const override { return TYPE; } \

struct ProgramDecl : public Declaration
{
    DECL_TYPE(Declaration::Type::PROGRAM);
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
    std::string callee {};
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
auto static constexpr CYAN = "\033[36m";
auto static constexpr RESET = "\033[00m";

class [[nodiscard]] ParserError
{
public:
    ENUM_CLASS(Type,
        FUNCTION_DECL_WITHOUT_NAME,
        UNEXPECTED_TOKEN_REACHED,
        EXPECTED_TOKEN_MISSING,
        ENCLOSING_TOKEN_MISSING,
        ENCLOSING_TOKEN_UNMATCHING,
        UNEXPECTED_END_OF_FILE
    )

public:
    using message_t = std::monostate;

    constexpr explicit ParserError(message_t reason) : reason_m { reason } {}

    constexpr  ParserError() noexcept = default;
    constexpr ~ParserError() noexcept = default;

    constexpr ParserError(ParserError const& error) : reason_m { error.reason_m } {}
    constexpr ParserError(ParserError&& error) noexcept : reason_m { std::move(error.reason_m) } {}

    constexpr ParserError& operator=(ParserError&& error) noexcept
    {
        reason_m = std::move(error.reason_m);
        return *this;
    }

    constexpr ParserError& operator=(ParserError const& error)
    {
        reason_m = error.reason_m;
        return *this;
    }

    [[nodiscard]] constexpr auto const& message() const noexcept { return reason_m; }

private:
    message_t reason_m;
};

bool hadAnError_g = false;

void emit_error(ParserError::Type const& error, std::vector<Token> const& tokens)
{
    hadAnError_g = true;

    std::vector<std::string> lines {};

    for (auto const& line : next_file_line(tokens.at(0).location.first))
    {
        lines.push_back(line.first);
    }

    switch (error)
    {
    case ParserError::Type::FUNCTION_DECL_WITHOUT_NAME: {
        auto& token = tokens.at(0);
        auto& [data, type, location, depth] = token;
        auto& [file, position] = location;
        auto& [line, column] = position;

        auto beforeToken = lines.at(line).substr(0, 1+column-data.size());
        auto afterToken  = column ? lines.at(line).substr(1+column) : "";

        std::cout << RED << "[error]" << RESET << ": function declared without a name\n";
        std::cout << '\n';
        std::cout << "at " << file.string() << ':' << line+1 << ':' << beforeToken.size() + 1 << '\n';
        std::cout << '\n';
        std::cout << "    " << " | " << '\n';

        auto index = beforeToken.find_first_not_of(' ');

        if (index != std::string::npos)
        {
            beforeToken = beforeToken.substr(index);
        }

        std::cout << GREEN << std::right << std::setw(4) << line+1 << RESET << " | " << beforeToken << BLUE << token.data << RESET << afterToken << '\n';
        std::cout << "    " << " | " << std::string(beforeToken.size(), ' ') << RED << std::string(data.size(), '^') << RESET << " is missing the 'name' property\n";

        std::cout << CYAN << "\nhint: adding 'name' property may solve this error\n" << RESET;

        break;
    }
    case ParserError::Type::UNEXPECTED_TOKEN_REACHED: {
        auto& token = tokens.at(0);
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

        std::cout << GREEN << std::right << std::setw(4) << line+1 << RESET << " | " << beforeToken << BLUE << token.data << RESET << afterToken << '\n';
        std::cout << "    " << " | " << std::string(beforeToken.size(), ' ') << RED << std::string(data.size(), '^') << RESET << " is invalid in this context\n";

        std::cout << CYAN << "\nhint: removing it may help\n" << RESET;

        break;
    }
    case ParserError::Type::EXPECTED_TOKEN_MISSING: {
        auto& token = tokens.at(0);
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

        std::cout << GREEN << std::right << std::setw(4) << line+1 << RESET << " | " << beforeToken << BLUE << token.data << RESET << afterToken << '\n';
        std::cout << "    " << " | " << std::string(beforeToken.size(), ' ') << RED << std::string(data.size(), '^') << RESET << " this token is missing a " << tokens.at(1).data << '\n';

        break;
    }
    case ParserError::Type::ENCLOSING_TOKEN_MISSING: {
        auto& token = tokens.at(0);
        auto& [data, type, location, depth] = token;
        auto& [file, position] = location;
        auto& [line, column] = position;

        auto beforeToken = lines.at(line).substr(0, 1+column-data.size());
        auto afterToken  = column ? lines.at(line).substr(1+column) : "";

        std::cout << RED << "[error]" << RESET << ": missing enclosing token\n";
        std::cout << '\n';
        std::cout << "at " << file.string() << ':' << line+1 << ':' << beforeToken.size() + 1<< '\n';
        std::cout << '\n';
        std::cout << "    " << " | " << '\n';

        auto index = beforeToken.find_first_not_of(' ');

        if (index != std::string::npos)
        {
            beforeToken = beforeToken.substr(index);
        }

        std::cout << GREEN << std::right << std::setw(4) << line+1 << RESET << " | " << beforeToken << BLUE << token.data << RESET << afterToken << '\n';
        std::cout << "    " << " | " << std::string(beforeToken.size(), ' ') << RED << std::string(data.size(), '^') << RESET << " isn't followed by a </" << token.data << ">\n";

        std::cout << CYAN << "\nhint: you seem to have broken tags somewhere\n" << RESET;

        break;
    }
    case ParserError::Type::ENCLOSING_TOKEN_UNMATCHING: {
        {
        auto& token = tokens.at(0);
        auto& [data, type, location, depth] = token;
        auto& [file, position] = location;
        auto& [line, column] = position;

        auto beforeToken = lines.at(line).substr(0, 1+column-data.size());
        auto afterToken  = column ? lines.at(line).substr(1+column) : "";

        std::cout << RED << "[error]" << RESET << ": unmatching tokens found\n";
        std::cout << '\n';
        std::cout << "at " << file.string() << ':' << line+1 << ':' << beforeToken.size() + 1<< '\n';
        std::cout << '\n';
        std::cout << "    " << " | " << '\n';

        auto index = beforeToken.find_first_not_of(' ');

        if (index != std::string::npos)
        {
            beforeToken = beforeToken.substr(index);
        }

        std::cout << GREEN << std::right << std::setw(4) << line+1 << RESET << " | " << beforeToken << BLUE << token.data << RESET << afterToken << '\n';
        std::cout << "    " << " | " << std::string(beforeToken.size(), ' ') << RED << std::string(data.size(), '^') << RESET << " this token\n";
        }
        std::cout << '\n';
        {
        auto& token = tokens.at(1);
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

        std::cout << GREEN << std::right << std::setw(4) << line+1 << RESET << " | " << beforeToken << BLUE << token.data << RESET << afterToken << '\n';
        std::cout << "    " << " | " << std::string(beforeToken.size(), ' ') << RED << std::string(data.size(), '^') << RESET << " doesn't match with this one\n";
        }

        break;
    }
    }

    std::cout << '\n';
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

Result<Token, ParserError> advance_if_present(std::vector<Token> const& tokens, int& cursor, Token::Type type)
{
    if (expect(tokens, cursor, type))
    {
        return tokens.at(static_cast<size_t>(cursor--));
    }

    return make_error<ParserError>({});
}

Result<Token, ParserError> advance_if_present(std::vector<Token> const& tokens, int& cursor, Token::Type type, std::string_view data)
{
    if (expect(tokens, cursor, type, data))
    {
        return tokens.at(static_cast<size_t>(cursor--));
    }

    return make_error<ParserError>({});
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

Result<std::unique_ptr<Node>, ParserError> parse_expression(std::vector<Token> const& tokens, int& cursor)
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

Result<std::unique_ptr<Node>, ParserError> parse_argument(std::vector<Token> const& tokens, int& cursor)
{
    auto argumentStmt = std::make_unique<ArgumentStmt>();

    if (!advance_if_present(tokens, cursor, Token::Type::LEFT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    auto token = advance_if_present(tokens, cursor, Token::Type::KEYWORD, "arg");

    if (!token)
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (peek_cursor(tokens, cursor).type == Token::Type::IDENTIFIER)
    {
        if (!advance_if_present(tokens, cursor, Token::Type::IDENTIFIER, "value"))
        {
            Token expected {};
            expected.type = Token::Type::IDENTIFIER;
            expected.data = "value";
            emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { *token, expected });
            return make_error<ParserError>({});
        }

        if (!advance_if_present(tokens, cursor, Token::Type::EQUAL))
        {
            Token expected {};
            expected.type = Token::Type::EQUAL;
            expected.data = "=";
            emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { peek_cursor(tokens, cursor, -1), expected });
            return make_error<ParserError>({});
        }

        if (!advance_if_present(tokens, cursor, Token::Type::QUOTE))
        {
            Token expected {};
            expected.type = Token::Type::QUOTE;
            expected.data = "\"\"";
            emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { peek_cursor(tokens, cursor, -1), expected });
            return make_error<ParserError>({});
        }

        argumentStmt->value = TRY(parse_expression(tokens, cursor));

        if (!advance_if_present(tokens, cursor, Token::Type::QUOTE))
        {
            Token expected {};
            expected.type = Token::Type::QUOTE;
            expected.data = "\"";
            emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { peek_cursor(tokens, cursor, -1), expected });
            return make_error<ParserError>({});
        }
    }

    if (!advance_if_present(tokens, cursor, Token::Type::RIGHT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
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
            Token expected {};
            expected.type = Token::Type::IDENTIFIER;
            expected.data = "value";
            emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { *token, expected });
            return make_error<ParserError>({});
        }
    }

    if (!advance_if_present(tokens, cursor, Token::Type::LEFT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::SLASH))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::KEYWORD, "arg"))
    {
        emit_error(ParserError::Type::ENCLOSING_TOKEN_MISSING, { *token });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::RIGHT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    return argumentStmt;
}

Result<std::unique_ptr<Node>, ParserError> parse_call(std::vector<Token> const& tokens, int& cursor)
{
    auto callStmt = std::make_unique<CallStmt>();

    if (!advance_if_present(tokens, cursor, Token::Type::LEFT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    auto token = advance_if_present(tokens, cursor, Token::Type::KEYWORD, "call");

    if (!token)
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::IDENTIFIER, "name"))
    {
        Token expected {};
        expected.type = Token::Type::IDENTIFIER;
        expected.data = "name";
        emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { *token, expected });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::EQUAL))
    {
        Token expected {};
        expected.type = Token::Type::EQUAL;
        expected.data = "=";
        emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { peek_cursor(tokens, cursor, -1), expected });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::QUOTE))
    {
        Token expected {};
        expected.type = Token::Type::QUOTE;
        expected.data = "\"\"";
        emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { peek_cursor(tokens, cursor, -1), expected });
        return make_error<ParserError>({});
    }

    auto callee = advance_if_present(tokens, cursor, Token::Type::LITERAL);

    if (!callee)
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    callStmt->callee = callee->data;

    if (!advance_if_present(tokens, cursor, Token::Type::QUOTE))
    {
        Token expected {};
        expected.type = Token::Type::QUOTE;
        expected.data = "\"";
        emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { *token, expected });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::RIGHT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    while (cursor > 0 && peek_cursor(tokens, cursor).depth > token->depth)
    {
        auto argument = parse_argument(tokens, cursor);

        if (argument.has_value() && argument.value())
        {
            callStmt->arguments.push_back(std::move(argument.value()));
            continue;
        }

        if (!argument.has_value())
        {
            synchronize(tokens, *token, cursor);
            continue;
        }

        break;
    }

    if (!advance_if_present(tokens, cursor, Token::Type::LEFT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::SLASH))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::KEYWORD, "call"))
    {
        emit_error(ParserError::Type::ENCLOSING_TOKEN_UNMATCHING, { *token, peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::RIGHT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    return callStmt;
}

Result<std::unique_ptr<Node>, ParserError> parse_let(std::vector<Token> const& tokens, int& cursor)
{
    auto letStmt = std::make_unique<LetStmt>();

    if (!advance_if_present(tokens, cursor, Token::Type::LEFT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    auto token = advance_if_present(tokens, cursor, Token::Type::KEYWORD, "let");

    if (!token)
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::IDENTIFIER, "name"))
    {
        Token expected {};
        expected.type = Token::Type::IDENTIFIER;
        expected.data = "name";
        emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { *token, expected });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::EQUAL))
    {
        Token expected {};
        expected.type = Token::Type::EQUAL;
        expected.data = "=";
        emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { peek_cursor(tokens, cursor, -1), expected });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::QUOTE))
    {
        Token expected {};
        expected.type = Token::Type::QUOTE;
        expected.data = "\"\"";
        emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { peek_cursor(tokens, cursor, -1), expected });
        return make_error<ParserError>({});
    }

    auto name = advance_if_present(tokens, cursor, Token::Type::LITERAL);

    if (!name)
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    letStmt->name = name->data;

    if (!advance_if_present(tokens, cursor, Token::Type::QUOTE))
    {
        Token expected {};
        expected.type = Token::Type::QUOTE;
        expected.data = "\"";
        emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { peek_cursor(tokens, cursor, -1), expected });
        return make_error<ParserError>({});
    }

    if (peek_cursor(tokens, cursor).type == Token::Type::IDENTIFIER)
    {
        if (!advance_if_present(tokens, cursor, Token::Type::IDENTIFIER, "value"))
        {
            Token expected {};
            expected.type = Token::Type::IDENTIFIER;
            expected.data = "value";
            emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { *token, expected });
            return make_error<ParserError>({});
        }

        if (!advance_if_present(tokens, cursor, Token::Type::EQUAL))
        {
            Token expected {};
            expected.type = Token::Type::EQUAL;
            expected.data = "=";
            emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { peek_cursor(tokens, cursor, -1), expected });
            return make_error<ParserError>({});
        }

        if (!advance_if_present(tokens, cursor, Token::Type::QUOTE))
        {
            Token expected {};
            expected.type = Token::Type::QUOTE;
            expected.data = "\"\"";
            emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { peek_cursor(tokens, cursor, -1), expected });
            return make_error<ParserError>({});
        }

        letStmt->value = TRY(parse_expression(tokens, cursor));

        if (!advance_if_present(tokens, cursor, Token::Type::QUOTE))
        {
            Token expected {};
            expected.type = Token::Type::QUOTE;
            expected.data = "\"";
            emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { peek_cursor(tokens, cursor, -1), expected });
            return make_error<ParserError>({});
        }
    }

    if (!advance_if_present(tokens, cursor, Token::Type::RIGHT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (!letStmt->value)
    {
        auto expression = parse_expression(tokens, cursor);

        if (expression.has_value() && expression.value())
        {
            letStmt->value = std::move(expression.value());
        }

        if (!expression.has_value())
        {
            synchronize(tokens, *token, cursor);
        }
    }

    if (!advance_if_present(tokens, cursor, Token::Type::LEFT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::SLASH))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::KEYWORD, "let"))
    {
        emit_error(ParserError::Type::ENCLOSING_TOKEN_UNMATCHING, { *token, peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::RIGHT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    return letStmt;
}

Result<std::unique_ptr<Node>, ParserError> parse_statement(std::vector<Token> const& tokens, int& cursor)
{
    if (peek_cursor(tokens, cursor, 1).data == "let")
    {
        return parse_let(tokens, cursor);
    }

    if (peek_cursor(tokens, cursor, 1).data == "call")
    {
        return parse_call(tokens, cursor);
    }

    return {};
}

bool next_is_statement(std::vector<Token> const& tokens, int& cursor)
{
    return
        peek_cursor(tokens, cursor, 1).data == "let" ||
        peek_cursor(tokens, cursor, 1).data == "call" ||
        peek_cursor(tokens, cursor, 1).data == "arg"
        ;
}

Result<std::unique_ptr<Node>, ParserError> parse_function(std::vector<Token> const& tokens, int& cursor)
{
    auto functionDecl = std::make_unique<FunctionDecl>();

    if (!advance_if_present(tokens, cursor, Token::Type::LEFT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    auto token = advance_if_present(tokens, cursor, Token::Type::KEYWORD, "function");

    if (!token)
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::IDENTIFIER, "name"))
    {
        Token expected {};
        expected.type = Token::Type::IDENTIFIER;
        expected.data = "name";
        emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { *token, expected });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::EQUAL))
    {
        Token expected {};
        expected.type = Token::Type::EQUAL;
        expected.data = "=";
        emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { peek_cursor(tokens, cursor, -1), expected });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::QUOTE))
    {
        Token expected {};
        expected.type = Token::Type::QUOTE;
        expected.data = "\"\"";
        emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { peek_cursor(tokens, cursor, -1), expected });
        return make_error<ParserError>({});
    }

    auto name = advance_if_present(tokens, cursor, Token::Type::LITERAL);

    if (!name)
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    functionDecl->name = name->data;

    if (!advance_if_present(tokens, cursor, Token::Type::QUOTE))
    {
        Token expected {};
        expected.type = Token::Type::QUOTE;
        expected.data = "\"";
        emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { peek_cursor(tokens, cursor, -1), expected });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::IDENTIFIER, "result"))
    {
        Token expected {};
        expected.type = Token::Type::IDENTIFIER;
        expected.data = "result";
        emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { *token, expected });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::EQUAL))
    {
        Token expected {};
        expected.type = Token::Type::EQUAL;
        expected.data = "=";
        emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { peek_cursor(tokens, cursor, -1), expected });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::QUOTE))
    {
        Token expected {};
        expected.type = Token::Type::QUOTE;
        expected.data = "\"\"";
        emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { peek_cursor(tokens, cursor, -1), expected });
        return make_error<ParserError>({});
    }

    auto result = advance_if_present(tokens, cursor, Token::Type::LITERAL);

    if (!result)
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    functionDecl->result = result->data;

    if (!advance_if_present(tokens, cursor, Token::Type::QUOTE))
    {
        Token expected {};
        expected.type = Token::Type::QUOTE;
        expected.data = "\"";
        emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { peek_cursor(tokens, cursor, -1), expected });
        return make_error<ParserError>({});
    }

    while (cursor > 0 && peek_cursor(tokens, cursor).type == Token::Type::IDENTIFIER)
    {
        auto parameterName = advance_if_present(tokens, cursor, Token::Type::IDENTIFIER);

        if (!parameterName)
        {
            emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
            return make_error<ParserError>({});
        }

        if (!advance_if_present(tokens, cursor, Token::Type::EQUAL))
        {
            Token expected {};
            expected.type = Token::Type::EQUAL;
            expected.data = "=";
            emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { *token, expected });
            return make_error<ParserError>({});
        }

        if (!advance_if_present(tokens, cursor, Token::Type::QUOTE))
        {
            Token expected {};
            expected.type = Token::Type::QUOTE;
            expected.data = "\"";
            emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { *token, expected });
            return make_error<ParserError>({});
        }

        auto parameterType = advance_if_present(tokens, cursor, Token::Type::LITERAL);

        if (!parameterType)
        {
            emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
            return make_error<ParserError>({});
        }

        if (!advance_if_present(tokens, cursor, Token::Type::QUOTE))
        {
            Token expected {};
            expected.type = Token::Type::QUOTE;
            expected.data = "\"";
            emit_error(ParserError::Type::EXPECTED_TOKEN_MISSING, { *token, expected });
            return make_error<ParserError>({});
        }

        functionDecl->parameters.push_back({ parameterName->data, parameterType->data });
    }

    if (!advance_if_present(tokens, cursor, Token::Type::RIGHT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    while (cursor > 0 && peek_cursor(tokens, cursor).depth > token->depth)
    {
        auto statement = parse_statement(tokens, cursor);

        if (statement.has_value() && statement.value())
        {
            functionDecl->scope.push_back(std::move(statement.value()));
            continue;
        }

        if (!statement.has_value())
        {
            synchronize(tokens, *token, cursor);
            continue;
        }

        break;
    }

    if (!advance_if_present(tokens, cursor, Token::Type::LEFT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::SLASH))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::KEYWORD, "function"))
    {
        emit_error(ParserError::Type::ENCLOSING_TOKEN_UNMATCHING, { *token, peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::RIGHT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    return functionDecl;
}

Result<std::unique_ptr<Node>, ParserError> parse_declaration(std::vector<Token> const& tokens, int& cursor)
{
    if (peek_cursor(tokens, cursor, 1).data == "function")
    {
        return TRY(parse_function(tokens, cursor));
    }

    return {};
}

bool next_is_declaration(std::vector<Token> const& tokens, int& cursor)
{
    return
        peek_cursor(tokens, cursor, 1).data == "function"
        ;
}

Result<std::unique_ptr<Node>, ParserError> parse_program(std::vector<Token> const& tokens, int& cursor)
{
    auto program = std::make_unique<ProgramDecl>();

    if (!advance_if_present(tokens, cursor, Token::Type::LEFT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    auto token = advance_if_present(tokens, cursor, Token::Type::KEYWORD, "program");

    if (!token)
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::RIGHT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    while (cursor > 0 && peek_cursor(tokens, cursor).depth == token->depth + 1)
    {
        Result<std::unique_ptr<Node>, ParserError> node;

        if (next_is_declaration(tokens, cursor)) node = parse_declaration(tokens, cursor);
        else if (next_is_statement(tokens, cursor)) node = parse_statement(tokens, cursor);

        if (node.has_value() && node.value())
        {
            program->scope.push_back(std::move(node.value()));
            continue;
        }

        if (!node.has_value())
        {
            synchronize(tokens, *token, cursor);
            continue;
        }

        break;
    }

    auto maybeMain = std::find_if(program->scope.begin(), program->scope.end(), [] (std::unique_ptr<Node>& node) {
        return node->node_type() == Node::Type::DECLARATION && static_cast<FunctionDecl*>(node.get())->name == "main";
    });

    if (maybeMain != program->scope.end())
    {
        auto call = std::make_unique<CallStmt>();
        call->callee = "main";
        program->scope.push_back(std::move(call));
    }

    if (!advance_if_present(tokens, cursor, Token::Type::LEFT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::SLASH))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::KEYWORD, "program"))
    {
        emit_error(ParserError::Type::ENCLOSING_TOKEN_UNMATCHING, { *token, peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    if (!advance_if_present(tokens, cursor, Token::Type::RIGHT_ANGLE))
    {
        emit_error(ParserError::Type::UNEXPECTED_TOKEN_REACHED, { peek_cursor(tokens, cursor) });
        return make_error<ParserError>({});
    }

    return program;
}

Result<std::unique_ptr<Node>, ParserError> parse(std::vector<Token> const& tokens)
{
    auto cursor = static_cast<int>(tokens.size()-1);
    return TRY(parse_program(tokens, cursor));
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
                    "statement", {
                        { "type", program->stmt_type().to_string() },
                        { "callee", program->callee },
                        { "arguments", nlohmann::json::array() }
                    }
                }};

                for (auto const& child : program->arguments)
                {
                    ast["statement"]["arguments"].push_back(dump_ast(child));
                }

                break;
            }
            case Statement::Type::ARGUMENT: {
                auto argument = static_cast<ArgumentStmt*>(statement);

                ast = {{
                    "statement", {
                        { "type", argument->stmt_type() },
                        { "value", dump_ast(argument->value) },
                    }
                }};

                break;
            }
            case Statement::Type::RETURN: {
                assert("UNIMPLEMENTED" && false);
                break;
            }
            case Statement::Type::LET: {
                auto let = static_cast<LetStmt*>(statement);

                ast = {{
                    "statement", {
                        { "type", let->stmt_type() },
                        { "name", let->name },
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
                        "expression", {
                            { "type", literal->expr_type() },
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
                    "declaration", {
                        { "type", program->decl_type() },
                        { "scope", nlohmann::json::array() }
                    }
                }};

                for (auto const& child : program->scope)
                {
                    ast["declaration"]["scope"].push_back(dump_ast(child));
                }

                break;
            }
            case Declaration::Type::FUNCTION: {
                auto function = static_cast<FunctionDecl*>(declaration);

                ast = {{
                    "declaration", {
                        { "type", function->decl_type() },
                        { "name", function->name },
                        { "result", function->result },
                        { "parameters", nlohmann::json::array() },
                        { "scope", nlohmann::json::array() }
                    }
                }};

                for (auto const& parameter : function->parameters)
                {
                    ast["declaration"]["parameters"].push_back({ { "name", parameter.first }, { "type", parameter.second } });
                }

                for (auto const& child : declaration->scope)
                {
                    ast["declaration"]["scope"].push_back(dump_ast(child));
                }

                break;
            }
            }

            break;
        }
    }

    return ast;
}

Result<void> interpret(std::unique_ptr<Node> const& node, std::vector<std::reference_wrapper<std::unique_ptr<Node>>> locals = {}, std::vector<std::reference_wrapper<std::unique_ptr<Node>>> globals = {})
{
    switch (node->node_type())
    {
        case Node::Type::STATEMENT: {
            auto statement = static_cast<Statement*>(node.get());

            switch (statement->stmt_type())
            {
            case Statement::Type::CALL: {
                auto call = static_cast<CallStmt*>(node.get());

                auto maybeCallee = std::find_if(globals.begin(), globals.end(), [&] (std::unique_ptr<Node> const& node) {
                    return static_cast<FunctionDecl*>(node.get())->name == call->callee;
                });

                if (maybeCallee != globals.end())
                {
                    TRY(interpret(*maybeCallee, locals, globals));
                }
                else
                {
                    if (call->callee == "print")
                    {
                        auto argument = static_cast<ArgumentStmt*>(call->arguments.at(0).get());
                        std::cout << static_cast<LiteralExpr*>(argument->value.get())->value;
                    }
                    else if (call->callee == "println")
                    {
                        auto argument = static_cast<ArgumentStmt*>(call->arguments.at(0).get());
                        std::cout << static_cast<LiteralExpr*>(argument->value.get())->value << '\n';
                    }
                }

                break;
            }
            case Statement::Type::LET: {
                assert("UNIMPLEMENTED" && false);
                break;
            }
            case Statement::Type::ARGUMENT: {
                assert("UNIMPLEMENTED" && false);
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

                std::vector<std::reference_wrapper<std::unique_ptr<Node>>> symbols {};

                for (auto& child : program->scope)
                {
                    if (child->node_type() == Node::Type::DECLARATION)
                    {
                        globals.push_back(child);
                    }
                }

                for (auto const& child : program->scope)
                {
                    if (child->node_type() == Node::Type::STATEMENT)
                    {
                        TRY(interpret(child, {}, globals));
                    }
                }

                break;
            }
            case Declaration::Type::FUNCTION: {
                auto function = static_cast<FunctionDecl*>(declaration);

                std::vector<std::reference_wrapper<std::unique_ptr<Node>>> symbols {};

                for (auto& child : function->scope)
                {
                    if (child->node_type() == Node::Type::STATEMENT && static_cast<Statement*>(child.get())->stmt_type() == Statement::Type::LET)
                    {
                        symbols.push_back(child);
                    }
                }

                for (auto const& child : function->scope)
                {
                    TRY(interpret(child, symbols, globals));
                }

                break;
            }
            }

            break;
        }
    }

    return {};
}

Result<void> safe_main(std::span<char const*> arguments)
{
    argparse::ArgumentParser args("xmlang", "", argparse::default_arguments::help);
    args.add_description("xmlang interpreter");

    args.add_argument("-f", "--file").help("xmlang script to be interpreted").required();
    args.add_argument("-d", "--dump").choices("ast", "tokens").help("dumps the given xmlang script");

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

    if (args.is_used("--dump") && args.get<std::string>("--dump") == "tokens")
    {
        std::cout << std::setw(4) << dump_tokens(tokens);
        return {};
    }

    auto ast = parse(tokens);

    if (hadAnError_g)
    {
        return make_error("I give up. ( ; ω ; )");
    }

    if (args.is_used("--dump") && args.get<std::string>("--dump") == "ast")
    {
        std::cout << std::setw(4) << dump_ast(ast.value()) << '\n';
        return {};
    }

    TRY(interpret(*ast));

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
