#include "codegen/Assembler.hpp"

#include <libcoro/Generator.hpp>
#include <libenum/Enum.hpp>
#include <liberror/Try.hpp>

#include <algorithm>
#include <cstring>
#include <functional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace liberror;
using namespace libcoro;

ENUM_CLASS(Instruction,
    PUSHA,
    PUSHB,
    POP,
    CALLA,
    CALLB,
    RET
);

ENUM_CLASS(Intrinsic, PRINT, PRINTLN, FORMAT);
ENUM_CLASS(Section, ARGUMENT, SCOPE, DATA);

static std::map<std::string, int32_t> codeSegmentOffsets_g;

static Generator<std::string> next_line(std::string_view code)
{
    std::istringstream stream(code.data());

    for (std::string line; std::getline(stream, line); )
    {
        co_yield line;
    }

    co_return;
}

std::array<uint8_t, 4> int_2_bytes(int value)
{
    return {
        static_cast<uint8_t>((value >> 24) & 0xFF),
        static_cast<uint8_t>((value >> 16) & 0xFF),
        static_cast<uint8_t>((value >> 8)  & 0xFF),
        static_cast<uint8_t>((value >> 0)  & 0xFF),
    };
}

std::pair<std::string, std::string> split_code_segments(std::string_view code)
{
    std::stringstream stream(code.data());

    std::string dataSegment;
    std::string codeSegment;

    for (auto bytes = 0zu; !stream.eof(); )
    {
        std::string line; std::getline(stream, line);

        if (line.starts_with('.') && bytes > 0)
        {
            dataSegment = code.substr(0, bytes);
            codeSegment = code.substr(bytes + code.substr(bytes).find_first_of('.'));

            break;
        }

        bytes += line.size();
        bytes += line.empty() ? 0 : 1;
    }

    return { dataSegment, codeSegment };
}

Result<std::vector<uint8_t>> assemble_data_segment(std::string_view code)
{
    std::vector<uint8_t> bytes {};

    std::istringstream stream(code.data());

    std::string segment; stream >> segment;

    if (segment != ".DATA")
    {
        return make_error("Unexpected segment '{}' was reached", segment);
    }

    while (!stream.eof())
    {
        int size; stream >> size;

        std::ignore = stream.get();

        std::string data; std::getline(stream, data);

        std::ranges::copy(int_2_bytes(size), std::back_inserter(bytes));
        std::ranges::transform(data, std::back_inserter(bytes), std::identity{});
    }

    return bytes;
}

std::array<uint8_t, 3> assemble_push_a(std::string const& code)
{
    int value;
    std::string segment;
    std::stringstream(code) >> value >> segment;

    return {
        Instruction::PUSHA,
        static_cast<uint8_t>(value),
        static_cast<uint8_t>(Section::from_string(segment)),
    };

}

std::array<uint8_t, 4> assemble_push_b(std::string const& code)
{
    std::regex pattern(R"((\[\.?(\w+) \+ (\w)\]) (\w+))");
    std::smatch match;
    std::regex_match(code, match, pattern);

    int offset;
    std::stringstream(match.str(3)) >> offset;

    return {
        Instruction::PUSHB,
        static_cast<uint8_t>(Section::from_string(match.str(2))),
        static_cast<uint8_t>(offset),
        static_cast<uint8_t>(Section::from_string(match.str(4)))
    };
}

std::array<uint8_t, 2> assemble_call_a(std::string const& code)
{
    std::string value;
    std::stringstream(code) >> value;

    return {
        Instruction::CALLA,
        static_cast<uint8_t>(codeSegmentOffsets_g.at(value))
    };
}

std::array<uint8_t, 2> assemble_call_b(std::string code)
{
    std::transform(code.begin(), code.end(), code.begin(), ::toupper);

    return {
        Instruction::CALLB,
        static_cast<uint8_t>(Intrinsic::from_string(code))
    };
}

std::array<uint8_t, 1> assemble_ret() { return { Instruction::RET }; }

std::array<uint8_t, 2> assemble_pop(std::string const& code)
{
    return {
        Instruction::POP,
        static_cast<uint8_t>(Section::from_string(code))
    };
}

std::vector<uint8_t> assemble_instruction(std::string const& code)
{
    std::vector<uint8_t> bytes {};

    std::string opcode;
    std::istringstream(code.data()) >> opcode;

    if (opcode != "RET")
    {
        auto operands = code.substr(code.find_first_of(' ') + 1);

        if (opcode == "PUSHA")
        {
            std::ranges::copy(assemble_push_a(operands), std::back_inserter(bytes));
        }
        else if (opcode == "PUSHB")
        {
            std::ranges::copy(assemble_push_b(operands), std::back_inserter(bytes));
        }
        else if (opcode == "CALLA")
        {
            std::ranges::copy(assemble_call_a(operands), std::back_inserter(bytes));
        }
        else if (opcode == "CALLB")
        {
            std::ranges::copy(assemble_call_b(operands), std::back_inserter(bytes));
        }
        else if (opcode == "POP")
        {
            std::ranges::copy(assemble_pop(operands), std::back_inserter(bytes));
        }
    }
    else
    {
        std::ranges::copy(assemble_ret(), std::back_inserter(bytes));
    }

    return bytes;
}

Result<std::vector<uint8_t>> assemble_code_segment(std::string_view code)
{
    std::vector<uint8_t> bytes {};

    auto reader = next_line(code);

    if (auto segment = reader.next(); segment != ".CODE")
    {
        return make_error("Unexpected segment '{}' was reached", segment);
    }

    reader.next();

    for (auto header : reader)
    {
        assert(header.starts_with("FUNCTION") || header.starts_with("ENTRYPOINT"));

        codeSegmentOffsets_g.insert({ header.starts_with("FUNCTION") ? header.substr(header.find_first_of(' ')+1) : "ENTRYPOINT", bytes.size() });

        reader.next();

        for (auto instruction : reader)
        {
            if (instruction.empty()) break;
            std::ranges::copy(assemble_instruction(instruction), std::back_inserter(bytes));
        }
    }

    return bytes;
}

Result<std::vector<uint8_t>> assemble(std::string_view code)
{
    std::vector<uint8_t> program {};

    auto [dataSegment, codeSegment] = split_code_segments(code);

    auto dataSegmentBytes = TRY(assemble_data_segment(dataSegment));
    auto codeSegmentBytes = TRY(assemble_code_segment(codeSegment));

    std::ranges::copy("This is a kubo program", std::back_inserter(program));
    program.pop_back();

    // dataSegmentStart offset
    std::ranges::copy(int_2_bytes(0), std::back_inserter(program));
    // codeSegmentStart offset
    std::ranges::copy(int_2_bytes(static_cast<int32_t>(dataSegmentBytes.size())), std::back_inserter(program));
    // entrypoint offset
    std::ranges::copy(int_2_bytes(codeSegmentOffsets_g.at("ENTRYPOINT")), std::back_inserter(program));

    std::ranges::copy(dataSegmentBytes, std::back_inserter(program));
    std::ranges::copy(codeSegmentBytes, std::back_inserter(program));

    return program;
}
