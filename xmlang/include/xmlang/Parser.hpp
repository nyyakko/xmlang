#pragma once

#include "Lexer.hpp"

#include <libenum/Enum.hpp>
#include <liberror/Result.hpp>

#include <vector>
#include <memory>
#include <vector>

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

liberror::Result<std::unique_ptr<Node>> parse(std::vector<Token> const& tokens);
nlohmann::ordered_json dump_ast(std::unique_ptr<Node> const& node);
