#pragma once

#include <libenum/Enum.hpp>
#include <liberror/Result.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <vector>

inline std::array KEYWORDS {
    "arg",
    "call",
    "function",
    "let",
    "program",
    "return",
    "if",
    "else",
};

using Location = std::pair<std::filesystem::path, std::pair<size_t, size_t>>;

struct Token
{
    // cppcheck-suppress [unknownMacro]
    ENUM_CLASS(Type,
        LEFT_ANGLE,
        RIGHT_ANGLE,
        DOUBLE_QUOTE,
        SINGLE_QUOTE,
        SLASH,
        EQUAL,
        KEYWORD,
        LITERAL,
        PROPERTY,
        END_OF_FILE
    )

    std::string data;
    Type type;
    Location location;
    size_t depth;
};

std::vector<Token> tokenize(std::filesystem::path const& path);
nlohmann::ordered_json dump_tokens(std::vector<Token> const& tokens);
