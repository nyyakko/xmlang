#include "Lexer.hpp"

#include <argparse/argparse.hpp>
#include <fmt/format.h>
#include <libcoro/Generator.hpp>
#include <liberror/Try.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace libcoro;
using namespace liberror;
using namespace std::literals;

static Generator<Token> next_token(std::string_view line)
{
    size_t depth = 0;

    for (auto cursor = 0zu; cursor < line.size(); cursor += 1)
    {
        size_t space = 0;

        while (cursor != line.size() && line.at(cursor) == ' ')
        {
            space += 1; cursor += 1;
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

            while (cursor < line.size())
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

            while (cursor < line.size())
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

            while (cursor < line.size())
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

static Generator<std::string> next_line(std::filesystem::path const& path)
{
    std::ifstream stream(path);

    for (std::string line; std::getline(stream, line); )
    {
        co_yield line;
    }

    co_return;
}

std::vector<Token> tokenize(std::filesystem::path const& path)
{
    std::vector<Token> tokens {};

    size_t lineNumber = 0;

    for (auto const& line : next_line(path))
    {
        for (auto token : next_token(line))
        {
            token.location.first = path;
            token.location.second.first = lineNumber;
            tokens.push_back(token);
        }

        lineNumber++;
    }

    tokens.push_back({
        .data = "EOF",
        .type = Token::Type::END_OF_FILE,
        .location = { path, { lineNumber-1, 0 } },
        .depth = 0
    });

    std::reverse(tokens.begin(), tokens.end());

    return tokens;
}

nlohmann::ordered_json dump_tokens(std::vector<Token> const& tokens)
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
