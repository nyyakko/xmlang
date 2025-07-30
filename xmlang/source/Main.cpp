#include "Compiler.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"

#include <argparse/argparse.hpp>
#include <fmt/format.h>
#include <libenum/Enum.hpp>
#include <liberror/Result.hpp>
#include <liberror/Try.hpp>

#include <filesystem>

using namespace liberror;

Result<void> safe_main(std::span<char const*> arguments)
{
    argparse::ArgumentParser args("xmlang", "", argparse::default_arguments::help);
    args.add_description("xmlang compiler");

    args.add_argument("-f", "--file").help("file to be compiled").required();
    args.add_argument("-d", "--dump").choices("ast", "tokens").help("dumps the given xmlang source");
    args.add_argument("--arch").choices("lmx").help("compilation target architecture");

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

    if (args.has_value("--arch"))
    {
        TRY(compile(ast, args.get<std::string>("--arch")));
    }
    else
    {
        TRY(compile(ast));
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
