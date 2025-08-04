#include "codegen/Assembler.hpp"

#include <libcoro/Generator.hpp>
#include <liberror/Try.hpp>
#include <magic_enum/magic_enum.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <regex>
#include <span>
#include <sstream>
#include <string>

using namespace liberror;
using namespace libcoro;

enum class Opcode
{
    CALL,
    LOAD,
    POP,
    PUSH,
    RET,
    STORE
};

enum class CallMode { EXTRINSIC, INTRINSIC };

enum class Intrinsic { PRINT, PRINTLN };

enum class DataSource { DATA_SEGMENT, LOCAL_SCOPE, GLOBAL_SCOPE };
enum class DataDestination { LOCAL_SCOPE, GLOBAL_SCOPE };

inline std::array<uint8_t, 4> int_2_bytes(int value)
{
    return {
        static_cast<uint8_t>((value >> 24) & 0xFF),
        static_cast<uint8_t>((value >> 16) & 0xFF),
        static_cast<uint8_t>((value >> 8)  & 0xFF),
        static_cast<uint8_t>((value >> 0)  & 0xFF),
    };
}

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

static Result<std::vector<uint8_t>> assemble_data_segment(std::string_view code)
{
    if (code.empty()) return {};

    std::vector<uint8_t> bytes {};

    auto reader = next_line(code);

    if (auto segment = reader.next(); segment != ".data")
    {
        return make_error("Unexpected segment '{}' was reached", segment);
    }

    reader.next();

    for (auto entry : reader)
    {
        int size;
        std::stringstream(entry.substr(0, entry.find_first_of(' '))) >> size;
        std::ranges::copy(int_2_bytes(size), std::back_inserter(bytes));
        std::ranges::transform(entry.substr(entry.find_first_of(' ')+1), std::back_inserter(bytes), std::identity{});
    }

    return bytes;
}

static std::array<uint8_t, 2> assemble_call(std::string const& operand)
{
    if (codeSegmentOffsets_g.contains(operand))
    {
        return {
            uint8_t(Opcode::CALL) << 3 | uint8_t(CallMode::EXTRINSIC),
            uint8_t(codeSegmentOffsets_g.at(operand))
        };
    }
    else
    {
        std::string intrinsic {};
        std::transform(operand.begin(), operand.end(), std::back_inserter(intrinsic), ::toupper);

        assert(magic_enum::enum_contains<Intrinsic>(intrinsic));

        return {
            uint8_t(Opcode::CALL) << 3 | uint8_t(CallMode::INTRINSIC),
            uint8_t(*magic_enum::enum_cast<Intrinsic>(intrinsic))
        };
    }
}

static std::array<uint8_t, 6> assemble_load(std::string const& operands)
{
    int offset;
    auto offsetData = operands.substr(operands.find_first_of('[')+1);
    offsetData.pop_back();
    std::stringstream(offsetData) >> offset;

    auto source = [&] {
        std::string source;
        std::stringstream(operands.substr(0, operands.find_first_of('['))) >> source;
        return
            source == ".data" ? DataSource::DATA_SEGMENT :
            source == "scope" ? DataSource::LOCAL_SCOPE  :
                                DataSource::GLOBAL_SCOPE;
    }();

    std::array<uint8_t, 6> result {
        uint8_t(Opcode::LOAD) << 3,
        uint8_t(source)
    };

    std::ranges::copy(int_2_bytes(offset), std::next(std::begin(result), 2));

    return result;
}

static std::array<uint8_t, 1> assemble_pop()
{
    return { uint8_t(Opcode::POP) << 3 };
}

static std::array<uint8_t, 5> assemble_push(std::string const& operand)
{
    int value;
    std::stringstream(operand) >> value;

    std::array<uint8_t, 5> result {
        uint8_t(Opcode::PUSH) << 3,
    };

    std::ranges::copy(int_2_bytes(value), std::next(std::begin(result), 1));

    return result;
}

static std::array<uint8_t, 1> assemble_ret()
{
    return { uint8_t(Opcode::RET) << 3 };
}

static std::array<uint8_t, 6> assemble_store(std::string const& operands)
{
    int offset;
    std::stringstream(operands.substr(operands.find_first_of('[')+1, 1)) >> offset;

    auto destination = [&] {
        std::string destination;
        std::stringstream(operands.substr(0, operands.find_first_of('['))) >> destination;
        return
            destination == "scope" ? DataDestination::LOCAL_SCOPE  :
                                     DataDestination::GLOBAL_SCOPE;
    }();

    std::array<uint8_t, 6> result {
        uint8_t(Opcode::STORE) << 3,
        uint8_t(destination)
    };

    std::ranges::copy(int_2_bytes(offset), std::next(std::begin(result), 2));

    return result;
}

static Result<std::vector<uint8_t>> assemble_code_segment(std::string_view code)
{
    std::vector<uint8_t> bytes {};

    auto reader = next_line(code);

    if (auto segment = reader.next(); segment != ".code")
    {
        return make_error("Unexpected segment '{}' was reached", segment);
    }

    reader.next();

    for (auto header : reader)
    {
        assert(header.starts_with("function") || header.starts_with("entrypoint"));

        codeSegmentOffsets_g.insert({ header.starts_with("function") ? header.substr(header.find_first_of(' ')+1) : "entrypoint", bytes.size() });

        reader.next();

        for (auto instruction : reader)
        {
            if (instruction.empty()) break;

            auto opcode = instruction.substr(0, instruction.find_first_of(' '));

            if (!(opcode == "pop" || opcode == "ret"))
            {
                auto operands = instruction.substr(instruction.find_first_of(' ') + 1);

                if      (opcode == "call") std::ranges::copy(assemble_call(operands), std::back_inserter(bytes));
                else if (opcode == "load") std::ranges::copy(assemble_load(operands), std::back_inserter(bytes));
                else if (opcode == "push") std::ranges::copy(assemble_push(operands), std::back_inserter(bytes));
                else if (opcode == "store") std::ranges::copy(assemble_store(operands), std::back_inserter(bytes));
                else
                {
                    return make_error("Unknown instruction '{}' was reached", instruction);
                }
            }
            else if (opcode == "pop")
            {
                std::ranges::copy(assemble_pop(), std::back_inserter(bytes));
            }
            else if (opcode == "ret")
            {
                std::ranges::copy(assemble_ret(), std::back_inserter(bytes));
            }
        }
    }

    return bytes;
}

Result<std::vector<uint8_t>> assemble(std::string const& code)
{
    std::stringstream stream(code);

    std::string dataSegment;
    std::string codeSegment;

    for (auto bytes = 0zu; !stream.eof(); )
    {
        std::string line; std::getline(stream, line);

        if ((line.starts_with('.') && bytes > 0) || line == ".code")
        {
            dataSegment = code.substr(0, bytes);
            codeSegment = code.substr(bytes + code.substr(bytes).find_first_of('.'));

            break;
        }

        bytes += line.size();
        bytes += line.empty() ? 0 : 1;
    }

    auto dataSegmentBytes = TRY(assemble_data_segment(dataSegment));
    auto codeSegmentBytes = TRY(assemble_code_segment(codeSegment));

    std::vector<uint8_t> program {};

    std::ranges::copy("This is a kubo program", std::back_inserter(program));
    program.pop_back();

    // dataSegmentStart offset
    std::ranges::copy(int_2_bytes(0), std::back_inserter(program));
    // codeSegmentStart offset
    std::ranges::copy(int_2_bytes(static_cast<int32_t>(dataSegmentBytes.size())), std::back_inserter(program));
    // entrypoint offset
    std::ranges::copy(int_2_bytes(codeSegmentOffsets_g.at("entrypoint")), std::back_inserter(program));

    std::ranges::copy(dataSegmentBytes, std::back_inserter(program));
    std::ranges::copy(codeSegmentBytes, std::back_inserter(program));

    return program;
}
