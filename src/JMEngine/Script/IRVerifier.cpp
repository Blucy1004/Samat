#include "JMEngine/Script/JMIR.hpp"
#include "JMEngine/Script/RuntimeABI.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <set>
#include <unordered_set>

namespace jm::script::ir {
namespace {
bool store(Op op) { return op == Op::Store || op == Op::GlobalStore; }
std::vector<ValueId> operands(const Instruction &in) {
    switch (in.op) {
    case Op::Constant:
    case Op::StringConstant:
    case Op::FloatConstant:
    case Op::Load:
    case Op::GlobalLoad:
        return {};
    case Op::Store:
    case Op::GlobalStore:
    case Op::Negate:
    case Op::LogicalNot:
    case Op::ToBoolean:
    case Op::FloatToInt:
    case Op::BitNot:
    case Op::IntToFloat:
        return {in.left};
    case Op::RuntimeCall:
    case Op::Call:
        return in.arguments;
    default:
        return {in.left, in.right};
    }
}
void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error("JM4001: " + message);
}
} // namespace
bool verify(const Module &module, LoweringDiagnostic &diagnostic) {
    try {
        require(!module.functions.empty(), "Module has no functions.");
        std::unordered_map<std::string, const Function *> functions;
        std::unordered_map<std::string, const Global *> globals;
        for (const auto &global : module.globals)
            require(globals.emplace(global.name, &global).second, "Duplicate global: " + global.name);
        for (const auto &fn : module.functions)
            require(functions.emplace(fn.name, &fn).second, "Duplicate function: " + fn.name);
        for (const auto &event : module.events)
            require(functions.contains(event.function) && functions.at(event.function)->parameterCount == 0 &&
                        functions.at(event.function)->returnType == Type::Void,
                    "Invalid event handler binding.");
        for (const auto &fn : module.functions) {
            const auto n = fn.blocks.size();
            require(n > 0, "Function has no entry block: " + fn.name);
            require(fn.localTypes.size() == fn.localCount && fn.valueTypes.size() == fn.valueCount,
                    "Type table size mismatch in " + fn.name);
            require(fn.parameterTypes.size() == fn.parameterCount && fn.parameterCount <= fn.localCount,
                    "Invalid parameters in " + fn.name);
            for (std::size_t i = 0; i < fn.parameterCount; ++i)
                require(fn.parameterTypes[i] == fn.localTypes[i] && fn.parameterTypes[i] != Type::Void,
                        "Parameter/local type mismatch or Void parameter.");
            std::vector<std::vector<BlockId>> predecessors(n), successors(n);
            std::unordered_map<ValueId, std::pair<BlockId, std::size_t>> definitions;
            for (std::size_t b = 0; b < n; ++b) {
                const auto &block = fn.blocks[b];
                require(block.id == b, "Block IDs must match their canonical index.");
                const auto &term = block.terminator;
                require(term.kind != Terminator::Kind::None, "Block has no terminator: " + block.name);
                if (term.kind == Terminator::Kind::Branch ||
                    term.kind == Terminator::Kind::ConditionalBranch) {
                    require(term.first < n, "Invalid branch target.");
                    successors[b].push_back(term.first);
                    predecessors[term.first].push_back(static_cast<BlockId>(b));
                    if (term.kind == Terminator::Kind::ConditionalBranch) {
                        require(term.second < n, "Invalid conditional branch target.");
                        successors[b].push_back(term.second);
                        predecessors[term.second].push_back(static_cast<BlockId>(b));
                    }
                }
                for (std::size_t i = 0; i < block.instructions.size(); ++i) {
                    const auto &in = block.instructions[i];
                    require(in.op >= Op::Constant && in.op <= Op::RuntimeCall, "Invalid opcode.");
                    if (!store(in.op)) {
                        require(in.result < fn.valueCount, "Invalid result value.");
                        require(definitions.emplace(in.result, std::pair{block.id, i}).second,
                                "Value is defined twice.");
                        require(in.type == fn.valueTypes[in.result],
                                "Result type differs from value type table.");
                    }
                    if (in.op == Op::Load || in.op == Op::Store)
                        require(in.local < fn.localCount, "Invalid local slot.");
                    if (in.op == Op::GlobalLoad || in.op == Op::GlobalStore)
                        require(globals.contains(in.symbol), "Unknown global: " + in.symbol);
                    if (in.op == Op::Call) {
                        require(in.argumentNames.empty() || in.argumentNames.size() == in.arguments.size(),
                                "Call argument name table size mismatch.");
                        if (auto target = functions.find(in.symbol); target != functions.end()) {
                            require(in.arguments.size() == target->second->parameterCount,
                                    "Invalid call arity: " + in.symbol);
                            require(in.type == target->second->returnType,
                                    "Call return type mismatch: " + in.symbol);
                        } else
                            require(
                                (in.symbol.rfind("builtin.", 0) == 0 ||
                                 (!in.symbol.empty() && in.symbolId == stableBuiltinSymbolId(in.symbol))) &&
                                    in.symbolId == stableBuiltinSymbolId(in.symbol),
                                "Unknown function: " + in.symbol);
                    }
                }
            }
            std::vector<bool> reachable(n);
            std::vector<BlockId> pending{0};
            while (!pending.empty()) {
                auto b = pending.back();
                pending.pop_back();
                if (reachable[b])
                    continue;
                reachable[b] = true;
                for (auto next : successors[b])
                    pending.push_back(next);
            }
            std::set<BlockId> all;
            for (std::size_t b = 0; b < n; ++b)
                if (reachable[b])
                    all.insert(static_cast<BlockId>(b));
            std::vector<std::set<BlockId>> dominators(n, all);
            dominators[0] = {0};
            bool changed = true;
            while (changed) {
                changed = false;
                for (std::size_t b = 1; b < n; ++b)
                    if (reachable[b]) {
                        auto next = all;
                        for (auto pred : predecessors[b])
                            if (reachable[pred]) {
                                std::set<BlockId> common;
                                std::set_intersection(next.begin(), next.end(), dominators[pred].begin(),
                                                      dominators[pred].end(),
                                                      std::inserter(common, common.begin()));
                                next = std::move(common);
                            }
                        next.insert(static_cast<BlockId>(b));
                        if (next != dominators[b]) {
                            dominators[b] = std::move(next);
                            changed = true;
                        }
                    }
            }
            auto operand = [&](ValueId id, BlockId block, std::size_t position) {
                require(id < fn.valueCount && definitions.contains(id),
                        "Operand refers to an undefined value.");
                const auto [definedBlock, definedPosition] = definitions.at(id);
                if (definedBlock == block)
                    require(definedPosition < position, "Value used before its definition.");
                else if (reachable[block])
                    require(dominators[block].contains(definedBlock),
                            "Value definition does not dominate its use.");
                return fn.valueTypes[id];
            };
            for (const auto &block : fn.blocks) {
                for (std::size_t i = 0; i < block.instructions.size(); ++i) {
                    const auto &in = block.instructions[i];
                    for (auto id : operands(in))
                        operand(id, block.id, i);
                    if (in.op == Op::Load || in.op == Op::Store)
                        require(in.type == fn.localTypes[in.local], "Local access type mismatch.");
                    if (in.op == Op::Store)
                        require(fn.valueTypes[in.left] == fn.localTypes[in.local],
                                "Store operand type mismatch.");
                    if (in.op == Op::GlobalLoad || in.op == Op::GlobalStore) {
                        require(in.type == globals.at(in.symbol)->type, "Global access type mismatch.");
                        if (in.op == Op::GlobalStore)
                            require(fn.valueTypes[in.left] == in.type, "Global store operand type mismatch.");
                    }
                    if (in.op == Op::FloatToInt)
                        require(fn.valueTypes[in.left] == Type::Float && in.type == Type::Int,
                                "Invalid Float-to-Int conversion.");
                    if (in.op >= Op::BitAnd && in.op <= Op::ShiftRight)
                        require(in.type == Type::Int && fn.valueTypes[in.left] == Type::Int &&
                                    (in.op == Op::BitNot || fn.valueTypes[in.right] == Type::Int),
                                "Bitwise operands must be Int.");
                    if (in.op == Op::IntToFloat)
                        require(fn.valueTypes[in.left] == Type::Int && in.type == Type::Float,
                                "Invalid numeric conversion.");
                    if (in.op == Op::Constant)
                        require(in.type == Type::Int || in.type == Type::Bool ||
                                    (in.type == Type::Null && in.immediate == 0),
                                "Invalid integer constant type.");
                    if (in.op == Op::StringConstant)
                        require(in.type == Type::String, "Invalid string constant type.");
                    if (in.op == Op::RuntimeCall) {
                        require(in.immediate >= JM_RT_CONCAT && in.immediate <= JM_RT_OPTIONAL_EQUAL,
                                "Invalid runtime operation.");
                        auto op = in.immediate;
                        size_t count = 1;
                        if (op == JM_RT_CONCAT || op == JM_RT_EQUAL || op == JM_RT_COMPARE ||
                            op == JM_RT_INDEX || op == JM_RT_CONTAINS || op == JM_RT_STARTS_WITH ||
                            op == JM_RT_ENDS_WITH || op == JM_RT_FIND || op == JM_RT_SPLIT ||
                            op == JM_RT_LIST_REMOVE_AT || op == JM_RT_TO_STRING || op == JM_RT_PRINT ||
                            op == JM_RT_PRINTLN)
                            count = 2;
                        if (op == JM_RT_LENGTH || op == JM_RT_SUBSTRING || op == JM_RT_REPLACE ||
                            op == JM_RT_LIST_GET || op == JM_RT_LIST_SET || op == JM_RT_LIST_PUSH ||
                            op == JM_RT_LIST_POP || op == JM_RT_LIST_CONTAINS || op == JM_RT_LIST_INDEX_OF ||
                            op == JM_RT_LIST_INSERT)
                            count = 3;
                        if (op >= JM_RT_TRUNC && op <= JM_RT_TIME_ELAPSED)
                            count =
                                op == JM_RT_ATAN2 || op == JM_RT_RANDOM_INT || op == JM_RT_RANDOM_FLOAT  ? 2
                                : op == JM_RT_LERP || op == JM_RT_SMOOTHSTEP                             ? 3
                                : op == JM_RT_RANDOM || op == JM_RT_TIME_NOW || op == JM_RT_TIME_ELAPSED ? 0
                                                                                                         : 1;
                        if (op >= JM_RT_OPTIONAL_NONE)
                            count = op == JM_RT_OPTIONAL_EQUAL                              ? 3
                                    : op == JM_RT_OPTIONAL_SOME || op == JM_RT_OPTIONAL_GET ? 2
                                                                                            : 1;
                        else if (op >= JM_RT_RANGE_CREATE)
                            count = op == JM_RT_RANGE_CREATE ? 3 : op == JM_RT_RANGE_LENGTH ? 1 : 2;
                        else if (op >= JM_RT_MAP_CREATE)
                            count =
                                op == JM_RT_MAP_SET ? 3
                                : op == JM_RT_MAP_GET || op == JM_RT_MAP_CONTAINS || op == JM_RT_MAP_REMOVE
                                    ? 2
                                    : 1;
                        else if (op >= JM_RT_RECORD_CREATE)
                            count = op == JM_RT_RECORD_CREATE ? 1 : op == JM_RT_RECORD_GET ? 2 : 3;
                        else if (op >= JM_RT_AGGREGATE_CREATE)
                            count = op == JM_RT_AGGREGATE_CREATE || op == JM_RT_VECTOR_LENGTH ||
                                            op == JM_RT_VECTOR_NORMALIZED
                                        ? 1
                                    : op == JM_RT_AGGREGATE_APPEND || op == JM_RT_VECTOR_LERP ? 3
                                                                                              : 2;
                        if (op == JM_RT_LIST_EQUAL)
                            count = 2;
                        require(in.arguments.size() == count, "Runtime ABI argument count mismatch.");
                        auto arg = [&](size_t index) { return fn.valueTypes[in.arguments.at(index)]; };
                        auto constant = [&](size_t index) -> int64_t {
                            auto id = in.arguments.at(index);
                            auto [block, position] = definitions.at(id);
                            auto &value = fn.blocks.at(block).instructions.at(position);
                            require(value.op == Op::Constant && value.type == Type::Int,
                                    "Runtime layout descriptor must be a constant Int.");
                            return value.immediate;
                        };
                        if (op == JM_RT_AGGREGATE_CREATE || op == JM_RT_RECORD_CREATE)
                            require(constant(0) == static_cast<int64_t>(in.type),
                                    "Aggregate construction type descriptor mismatch.");
                        if (op == JM_RT_AGGREGATE_APPEND)
                            require(constant(2) == static_cast<int64_t>(arg(0)),
                                    "Vector construction type descriptor mismatch.");
                        if (op == JM_RT_RECORD_APPEND)
                            require(constant(2) == static_cast<int64_t>(arg(1)),
                                    "Record field type descriptor mismatch.");
                        if (op == JM_RT_RECORD_GET || op == JM_RT_RECORD_SET)
                            require((static_cast<uint64_t>(constant(1)) >> 32) ==
                                        static_cast<uint64_t>(op == JM_RT_RECORD_GET ? in.type : arg(2)),
                                    "Record field payload type mismatch.");
                        if (op == JM_RT_MAP_CREATE)
                            require(fn.valueElementTypes.contains(in.result) &&
                                        constant(0) ==
                                            static_cast<int64_t>(fn.valueElementTypes.at(in.result)),
                                    "Map creation value metadata mismatch.");
                        if (op >= JM_RT_MAP_GET && op <= JM_RT_MAP_VALUES) {
                            require(fn.valueElementTypes.contains(in.arguments.at(0)),
                                    "Map value metadata missing.");
                            auto element = fn.valueElementTypes.at(in.arguments.at(0));
                            require(element == Type::Int || element == Type::Float || element == Type::Bool ||
                                        element == Type::String,
                                    "Invalid Map value metadata.");
                            if (op == JM_RT_MAP_GET)
                                require(in.type == element, "Map lookup result type mismatch.");
                            if (op == JM_RT_MAP_SET)
                                require(arg(2) == element, "Map write value type mismatch.");
                        }

                        if ((op >= JM_RT_CONCAT && op <= JM_RT_LOWER) || op == JM_RT_CODEPOINT_LENGTH ||
                            op == JM_RT_PARSE_INT || op == JM_RT_PARSE_FLOAT)
                            require(arg(0) == Type::String || (op == JM_RT_LENGTH && arg(0) == Type::List),
                                    "Runtime string receiver mismatch.");
                        if ((op >= JM_RT_LIST_GET && op <= JM_RT_LIST_REMOVE_AT) || op == JM_RT_LIST_CLONE)
                            require(arg(0) == Type::List, "Runtime list receiver mismatch.");
                        if (op == JM_RT_INDEX || op == JM_RT_SUBSTRING || op == JM_RT_LIST_GET ||
                            op == JM_RT_LIST_SET || op == JM_RT_LIST_INSERT || op == JM_RT_LIST_REMOVE_AT)
                            require(arg(1) == Type::Int, "Runtime index must be Int.");
                        if (op == JM_RT_CONCAT || op == JM_RT_EQUAL || op == JM_RT_COMPARE ||
                            op == JM_RT_CONTAINS || op == JM_RT_STARTS_WITH || op == JM_RT_ENDS_WITH ||
                            op == JM_RT_FIND || op == JM_RT_REPLACE || op == JM_RT_SPLIT)
                            require(arg(1) == Type::String, "Runtime string operand mismatch.");
                        if (op == JM_RT_REPLACE)
                            require(arg(2) == Type::String, "Runtime replace operand mismatch.");
                        if (op == JM_RT_SUBSTRING)
                            require(arg(2) == Type::Int, "Runtime substring count must be Int.");
                        if (op >= JM_RT_TRUNC && op <= JM_RT_TIME_ELAPSED) {
                            for (size_t i = 0; i < count; ++i)
                                require(arg(i) == (op == JM_RT_SEED || op == JM_RT_RANDOM_INT ? Type::Int
                                                                                              : Type::Float),
                                        "Runtime numeric argument mismatch.");
                            require(in.type == (op == JM_RT_SEED         ? Type::Void
                                                : op == JM_RT_RANDOM_INT ? Type::Int
                                                                         : Type::Float),
                                    "Runtime numeric result mismatch.");
                        }
                        if (op >= JM_RT_OPTIONAL_NONE) {
                            if (op == JM_RT_OPTIONAL_EQUAL) {
                                require(arg(0) == Type::Optional && arg(1) == Type::Optional &&
                                            arg(2) == Type::Int && in.type == Type::Bool &&
                                            fn.valueElementTypes.contains(in.arguments[0]) &&
                                            fn.valueElementTypes.contains(in.arguments[1]),
                                        "Invalid Optional equality.");
                                auto element = fn.valueElementTypes.at(in.arguments[0]);
                                require(element == fn.valueElementTypes.at(in.arguments[1]) &&
                                            constant(2) == static_cast<int64_t>(element),
                                        "Optional equality payload mismatch.");
                            } else if (op == JM_RT_OPTIONAL_NONE || op == JM_RT_OPTIONAL_SOME) {
                                require(in.type == Type::Optional && arg(0) == Type::Int &&
                                            fn.valueElementTypes.contains(in.result),
                                        "Invalid Optional construction.");
                                auto element = fn.valueElementTypes.at(in.result);
                                require(constant(0) == static_cast<int64_t>(element) &&
                                            element != Type::Any && element != Type::Void &&
                                            element != Type::Optional && element != Type::Null,
                                        "Optional payload descriptor mismatch.");
                                if (op == JM_RT_OPTIONAL_SOME)
                                    require(arg(1) == element, "Optional payload type mismatch.");
                            } else {
                                require(arg(0) == Type::Optional &&
                                            fn.valueElementTypes.contains(in.arguments[0]),
                                        "Invalid Optional receiver.");
                                if (op == JM_RT_OPTIONAL_HAS)
                                    require(in.type == Type::Bool, "Invalid Optional presence result.");
                                else
                                    require(arg(1) == Type::Int &&
                                                constant(1) == static_cast<int64_t>(in.type) &&
                                                in.type == fn.valueElementTypes.at(in.arguments[0]),
                                            "Invalid Optional unwrap type.");
                            }
                        } else if (op >= JM_RT_RANGE_CREATE) {
                            if (op == JM_RT_RANGE_CREATE)
                                require(arg(0) == Type::Int && arg(1) == Type::Int && arg(2) == Type::Int &&
                                            in.type == Type::Range,
                                        "Invalid Range construction.");
                            else {
                                require(arg(0) == Type::Range && in.type == Type::Int,
                                        "Invalid Range receiver/result.");
                                if (count == 2)
                                    require(arg(1) == Type::Int, "Invalid Range index.");
                            }
                        } else if (op >= JM_RT_MAP_CREATE) {
                            if (op == JM_RT_MAP_CREATE)
                                require(arg(0) == Type::Int && in.type == Type::Map, "Invalid Map creation.");
                            else {
                                require(arg(0) == Type::Map, "Invalid Map receiver.");
                                if (op == JM_RT_MAP_GET || op == JM_RT_MAP_SET || op == JM_RT_MAP_CONTAINS ||
                                    op == JM_RT_MAP_REMOVE)
                                    require(arg(1) == Type::String, "Map key must be String.");
                                if (op == JM_RT_MAP_SET || op == JM_RT_MAP_CLEAR)
                                    require(in.type == Type::Void, "Invalid Map mutation result.");
                                if (op == JM_RT_MAP_CONTAINS || op == JM_RT_MAP_REMOVE)
                                    require(in.type == Type::Bool, "Invalid Map Bool result.");
                                if (op == JM_RT_MAP_LENGTH)
                                    require(in.type == Type::Int, "Invalid Map length result.");
                                if (op == JM_RT_MAP_KEYS || op == JM_RT_MAP_VALUES)
                                    require(in.type == Type::List, "Invalid Map collection result.");
                            }
                        } else if (op >= JM_RT_RECORD_CREATE) {
                            if (op == JM_RT_RECORD_CREATE)
                                require(arg(0) == Type::Int &&
                                            (in.type == Type::Struct || in.type == Type::Tuple),
                                        "Invalid record creation.");
                            else {
                                require(arg(0) == Type::Struct || arg(0) == Type::Tuple,
                                        "Invalid record receiver.");
                                if (op == JM_RT_RECORD_APPEND)
                                    require(arg(2) == Type::Int && in.type == Type::Void,
                                            "Invalid record append.");
                                else {
                                    require(arg(1) == Type::Int, "Invalid record field index.");
                                    if (op == JM_RT_RECORD_SET)
                                        require(arg(0) == Type::Struct && in.type == Type::Void,
                                                "Immutable record write.");
                                }
                            }
                        } else if (op >= JM_RT_AGGREGATE_CREATE) {
                            auto aggregate = [](Type t) {
                                return t == Type::Vector2 || t == Type::Vector3 || t == Type::Color;
                            };
                            if (op == JM_RT_AGGREGATE_CREATE)
                                require(arg(0) == Type::Int && aggregate(in.type),
                                        "Invalid aggregate creation.");
                            else {
                                require(aggregate(arg(0)), "Invalid aggregate receiver.");
                                if (op == JM_RT_AGGREGATE_APPEND)
                                    require(arg(1) == Type::Float && arg(2) == Type::Int &&
                                                in.type == Type::Void,
                                            "Invalid aggregate append.");
                                else if (op == JM_RT_FIELD_GET)
                                    require(arg(1) == Type::Int && in.type == Type::Float,
                                            "Invalid aggregate field.");
                                else if (op == JM_RT_VECTOR_LENGTH)
                                    require(in.type == Type::Float && arg(0) != Type::Color,
                                            "Invalid vector length.");
                                else if (op == JM_RT_VECTOR_NORMALIZED)
                                    require(in.type == arg(0) && arg(0) != Type::Color,
                                            "Invalid normalization.");
                                else if (op == JM_RT_VECTOR_SCALE || op == JM_RT_VECTOR_DIVIDE)
                                    require(arg(1) == Type::Float && in.type == arg(0) &&
                                                arg(0) != Type::Color,
                                            "Invalid vector scale.");
                                else {
                                    require(arg(0) == arg(1), "Aggregate operands mismatch.");
                                    require(in.type ==
                                                (op == JM_RT_AGGREGATE_EQUAL ? Type::Bool
                                                 : op == JM_RT_VECTOR_DOT || op == JM_RT_VECTOR_DISTANCE
                                                     ? Type::Float
                                                     : arg(0)),
                                            "Aggregate result mismatch.");
                                    if (op == JM_RT_VECTOR_CROSS)
                                        require(arg(0) == Type::Vector3, "Cross requires Vector3.");
                                    if (op == JM_RT_VECTOR_LERP)
                                        require(arg(2) == Type::Float, "Lerp factor must be Float.");
                                }
                            }
                        }
                        if (op == JM_RT_LIST_EQUAL)
                            require(arg(0) == Type::List && arg(1) == Type::List && in.type == Type::Bool,
                                    "Runtime list equality type mismatch.");
                        if (op == JM_RT_LIST_CREATE)
                            require(arg(0) == Type::Int && in.type == Type::List,
                                    "Runtime list creation mismatch.");
                        if (op == JM_RT_CONCAT || op == JM_RT_INDEX || op == JM_RT_SUBSTRING ||
                            op == JM_RT_REPLACE || op == JM_RT_TRIM || op == JM_RT_UPPER ||
                            op == JM_RT_LOWER || op == JM_RT_TO_STRING)
                            require(in.type == Type::String, "Runtime String result mismatch.");
                        if (op == JM_RT_EQUAL || op == JM_RT_CONTAINS || op == JM_RT_STARTS_WITH ||
                            op == JM_RT_ENDS_WITH || op == JM_RT_LIST_CONTAINS)
                            require(in.type == Type::Bool, "Runtime Bool result mismatch.");
                        if (op == JM_RT_LIST_PUSH || op == JM_RT_LIST_SET || op == JM_RT_LIST_CLEAR ||
                            op == JM_RT_LIST_REVERSE || op == JM_RT_LIST_SORT || op == JM_RT_LIST_INSERT ||
                            op == JM_RT_PRINT || op == JM_RT_PRINTLN)
                            require(in.type == Type::Void, "Runtime Void result mismatch.");
                    }
                    if (in.op == Op::FloatConstant)
                        require(in.type == Type::Float, "Invalid float constant type.");
                    if (in.op == Op::Negate)
                        require(in.type == fn.valueTypes[in.left] &&
                                    (in.type == Type::Int || in.type == Type::Float),
                                "Invalid negate type.");
                    if (in.op == Op::LogicalNot || in.op == Op::ToBoolean)
                        require(in.type == Type::Bool && (fn.valueTypes[in.left] == Type::Int ||
                                                          fn.valueTypes[in.left] == Type::Float ||
                                                          fn.valueTypes[in.left] == Type::Bool),
                                "Invalid boolean conversion.");
                    if (in.op >= Op::Equal && in.op <= Op::GreaterEqual)
                        require(in.type == Type::Bool && fn.valueTypes[in.left] == fn.valueTypes[in.right],
                                "Invalid comparison types.");
                    if (in.op == Op::BooleanAnd || in.op == Op::BooleanOr)
                        require(in.type == Type::Bool && fn.valueTypes[in.left] == Type::Bool &&
                                    fn.valueTypes[in.right] == Type::Bool,
                                "Invalid boolean operand types.");
                    if (in.op == Op::GlobalStore && globals.at(in.symbol)->constant)
                        require(fn.name == "__jm_init",
                                "Cannot store a constant global outside initialization.");
                    if (in.op == Op::Call)
                        if (auto target = functions.find(in.symbol); target != functions.end())
                            for (std::size_t a = 0; a < in.arguments.size(); ++a)
                                require(fn.valueTypes[in.arguments[a]] == target->second->parameterTypes[a],
                                        "Call parameter type mismatch.");
                    if (in.op >= Op::Add && in.op <= Op::Modulo)
                        require(fn.valueTypes[in.left] == fn.valueTypes[in.right] &&
                                    in.type == fn.valueTypes[in.left] &&
                                    (in.type == Type::Int || in.type == Type::Float),
                                "Invalid arithmetic operand type.");
                }
                const auto &term = block.terminator;
                if (term.kind == Terminator::Kind::Return ||
                    term.kind == Terminator::Kind::ConditionalBranch) {
                    auto type = operand(term.value, block.id, block.instructions.size());
                    if (term.kind == Terminator::Kind::Return && fn.returnType != Type::Void)
                        require(type == fn.returnType, "Return type mismatch.");
                    if (term.kind == Terminator::Kind::ConditionalBranch)
                        require(type == Type::Int || type == Type::Bool || type == Type::Float,
                                "Invalid condition type.");
                }
            }
        }
        diagnostic.message.clear();
        return true;
    } catch (const std::exception &error) {
        diagnostic.message = error.what();
        return false;
    }
}
Module optimize(const Module &module) {
    LoweringDiagnostic diagnostic;
    if (!verify(module, diagnostic))
        throw std::runtime_error(diagnostic.message);
    Module result = module;
    for (auto &fn : result.functions) {
        std::unordered_map<ValueId, std::int64_t> constants;
        for (auto &block : fn.blocks)
            for (auto &in : block.instructions) {
                if (in.op == Op::Constant) {
                    constants[in.result] = in.immediate;
                    continue;
                }
                if (in.type == Type::Float || store(in.op))
                    continue;
                if (!constants.contains(in.left))
                    continue;
                const auto a = constants[in.left];
                std::int64_t value{};
                bool folded = true;
                if (in.op == Op::Negate)
                    value = std::bit_cast<std::int64_t>(std::uint64_t{0} - static_cast<std::uint64_t>(a));
                else if (in.op == Op::LogicalNot)
                    value = !a;
                else if (in.op == Op::ToBoolean)
                    value = a != 0;
                else if (!constants.contains(in.right))
                    continue;
                else {
                    const auto b = constants[in.right];
                    const auto ua = static_cast<std::uint64_t>(a), ub = static_cast<std::uint64_t>(b);
                    switch (in.op) {
                    case Op::Add:
                        value = std::bit_cast<std::int64_t>(ua + ub);
                        break;
                    case Op::Subtract:
                        value = std::bit_cast<std::int64_t>(ua - ub);
                        break;
                    case Op::Multiply:
                        value = std::bit_cast<std::int64_t>(ua * ub);
                        break;
                    case Op::Divide:
                    case Op::Modulo:
                        if (b == 0 || (a == std::numeric_limits<std::int64_t>::min() && b == -1))
                            folded = false;
                        else
                            value = in.op == Op::Divide ? a / b : a % b;
                        break;
                    case Op::Equal:
                        value = a == b;
                        break;
                    case Op::NotEqual:
                        value = a != b;
                        break;
                    case Op::Less:
                        value = a < b;
                        break;
                    case Op::LessEqual:
                        value = a <= b;
                        break;
                    case Op::Greater:
                        value = a > b;
                        break;
                    case Op::GreaterEqual:
                        value = a >= b;
                        break;
                    case Op::BooleanAnd:
                        value = a && b;
                        break;
                    case Op::BooleanOr:
                        value = a || b;
                        break;
                    default:
                        folded = false;
                        break;
                    }
                }
                if (folded) {
                    in.op = Op::Constant;
                    in.immediate = value;
                    in.arguments.clear();
                    constants[in.result] = value;
                }
            }
    }
    if (!verify(result, diagnostic))
        throw std::runtime_error(diagnostic.message);
    return result;
}
} // namespace jm::script::ir
