#include "JMEngine/Script/JMIR.hpp"
#include "JMEngine/Script/RuntimeABI.h"
#include "JMEngine/Script/StandardLibrary.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace jm::script::ir {
static_assert(static_cast<int>(Type::Int) == JM_RT_INT && static_cast<int>(Type::Float) == JM_RT_FLOAT &&
                  static_cast<int>(Type::String) == JM_RT_STRING &&
                  static_cast<int>(Type::List) == JM_RT_LIST,
              "Language types must preserve runtime ABI version 1 tags.");
namespace {

Type inferredExpression(const ExpressionPtr &value, const std::unordered_map<std::string, Type> &variables,
                        const std::unordered_map<std::string, Type> &functions) {
    if (!value)
        return Type::Void;
    switch (value->kind) {
    case Expression::Kind::Literal:
        return value->literal.type();
    case Expression::Kind::Identifier: {
        auto found = variables.find(value->text);
        return found == variables.end()
                   ? (value->text == "PI" || value->text == "E" ? Type::Float : Type::Int)
                   : found->second;
    }
    case Expression::Kind::Tuple:
        return Type::Tuple;
    case Expression::Kind::Array:
        return Type::List;
    case Expression::Kind::Map:
        return Type::Map;
    case Expression::Kind::Index:
        if (inferredExpression(value->left, variables, functions) == Type::String)
            return Type::String;
        if (value->left && value->left->kind == Expression::Kind::Identifier &&
            variables.contains("$element." + value->left->text))
            return variables.at("$element." + value->left->text);
        return Type::Any;
    case Expression::Kind::Member:
        if (auto standard = standardFunction(value->builtinSymbolName))
            return standard->returnType;
        if (value->builtinSymbolName == "builtin.math.PI" || value->builtinSymbolName == "builtin.math.E")
            return Type::Float;
        return Type::Int;
    case Expression::Kind::Unary:
        if (value->text == "!" || value->text == "not")
            return Type::Bool;
        return inferredExpression(value->right, variables, functions);
    case Expression::Kind::Binary: {
        if (value->text == "**" || value->text == "^")
            return Type::Float;
        if (value->text == "==" || value->text == "!=" || value->text == "<" || value->text == "<=" ||
            value->text == ">" || value->text == ">=" || value->text == "and" || value->text == "or" ||
            value->text == "&&" || value->text == "||")
            return Type::Bool;
        const auto a = inferredExpression(value->left, variables, functions),
                   b = inferredExpression(value->right, variables, functions);
        return a == Type::Float || b == Type::Float ? Type::Float : a;
    }
    case Expression::Kind::Call:
        if (functions.contains(value->builtinSymbolName))
            return functions.at(value->builtinSymbolName);
        if (value->left && value->left->kind == Expression::Kind::Member) {
            auto method = value->left->text;
            auto receiver = value->left->left;
            if (method == "contains" || method == "startsWith" || method == "endsWith")
                return Type::Bool;
            if (method == "find" || method == "indexOf" || method == "codepointLength")
                return Type::Int;
            if (method == "push" || method == "append" || method == "clear" || method == "insert" ||
                method == "sort" || method == "reverse")
                return Type::Void;
            if (method == "split")
                return Type::List;
            if (inferredExpression(receiver, variables, functions) == Type::String)
                return Type::String;
            if (receiver && receiver->kind == Expression::Kind::Identifier &&
                variables.contains("$element." + receiver->text))
                return variables.at("$element." + receiver->text);
        }
        if (value->left && value->left->kind == Expression::Kind::Identifier) {
            const auto &name = value->left->text;
            if (name == "int" || name == "bitXor")
                return Type::Int;
            if (name == "float")
                return Type::Float;
            if (name == "string")
                return Type::String;
            if (name == "len" || name == "length")
                return Type::Int;
        }
        if (value->left && value->left->kind == Expression::Kind::Identifier) {
            if (auto found = functions.find(value->left->text); found != functions.end())
                return found->second;
            if (auto standard = standardFunction(value->left->text))
                return standard->returnType == Type::Any && !value->arguments.empty()
                           ? inferredExpression(value->arguments[0].value, variables, functions)
                           : standard->returnType;
        }
        return Type::Int;
    }
    return Type::Int;
}
Type inferredReturn(const StatementList &list, std::unordered_map<std::string, Type> variables,
                    const std::unordered_map<std::string, Type> &functions) {
    Type result = Type::Void;
    auto merge = [&](Type type) {
        if (result == Type::Void)
            result = type;
        else if (type != Type::Void && result != type) {
            if ((result == Type::Int && type == Type::Float) || (result == Type::Float && type == Type::Int))
                result = Type::Float;
            else
                result = Type::Any;
        }
    };
    for (const auto &statement : list) {
        if (statement.kind == Statement::Kind::Variable)
            variables[statement.name] = statement.declaredType == Type::Any
                                            ? inferredExpression(statement.expression, variables, functions)
                                            : statement.declaredType;
        if (statement.kind == Statement::Kind::Variable && statement.expression) {
            const auto &value = statement.expression;
            if (value->kind == Expression::Kind::Array && !value->elements.empty()) {
                auto type = inferredExpression(value->elements[0], variables, functions);
                for (const auto &element : value->elements)
                    if (inferredExpression(element, variables, functions) == Type::Float)
                        type = Type::Float;
                variables["$element." + statement.name] = type;
            }
            if (value->kind == Expression::Kind::Identifier && variables.contains("$element." + value->text))
                variables["$element." + statement.name] = variables.at("$element." + value->text);
            if (value->kind == Expression::Kind::Call && value->left &&
                value->left->kind == Expression::Kind::Member && value->left->text == "split")
                variables["$element." + statement.name] = Type::String;
        }
        if (statement.elementType != Type::Any)
            variables["$element." + statement.name] = statement.elementType;
        if (statement.kind == Statement::Kind::ForRange)
            variables[statement.name] = Type::Int;
        if (statement.kind == Statement::Kind::Return)
            merge(inferredExpression(statement.expression, variables, functions));
        if (!statement.body.empty())
            merge(inferredReturn(statement.body, variables, functions));
        if (!statement.alternative.empty())
            merge(inferredReturn(statement.alternative, variables, functions));
    }
    return result;
}
bool guaranteedReturn(const StatementList &list) {
    for (const auto &item : list)
        if (item.kind == Statement::Kind::Return ||
            (item.kind == Statement::Kind::If && guaranteedReturn(item.body) &&
             guaranteedReturn(item.alternative)))
            return true;
    return false;
}
struct Lowerer {
    Function function;
    std::vector<std::unordered_map<std::string, std::uint32_t>> scopes;
    BlockId current{};
    std::uint32_t nextValue{};
    std::uint32_t nextBlock{};
    const Module *module{};
    const NativeFunctionRegistry *registry{};
    std::unordered_map<std::string, const Statement *> declarations;
    std::vector<std::pair<BlockId, BlockId>> loops;
    std::unordered_map<std::uint32_t, bool> constants;
    std::unordered_map<ValueId, Type> elements;
    std::unordered_map<std::uint32_t, Type> localElements;

    BlockId block(const std::string &name) {
        const BlockId id = nextBlock++;
        function.blocks.push_back({id, name, {}, {}});
        return id;
    }
    BasicBlock &at(BlockId id) { return function.blocks.at(id); }
    void select(BlockId id) { current = id; }
    ValueId emit(Instruction instruction) {
        instruction.result = nextValue++;
        function.valueTypes.push_back(instruction.type);
        at(current).instructions.push_back(std::move(instruction));
        return at(current).instructions.back().result;
    }
    std::uint32_t declare(const std::string &name, Type type = Type::Int, bool constant = false) {
        if (scopes.back().contains(name))
            throw std::runtime_error("Duplicate local variable: " + name);
        const auto slot = function.localCount++;
        function.localTypes.push_back(type);
        constants[slot] = constant;
        scopes.back()[name] = slot;
        return slot;
    }
    std::uint32_t local(const std::string &name) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            const auto found = it->find(name);
            if (found != it->end())
                return found->second;
        }
        throw std::runtime_error("Native lowering cannot resolve variable '" + name + "'.");
    }
    const Global *global(const std::string &name) const {
        if (module)
            for (const auto &item : module->globals)
                if (item.name == name)
                    return &item;
        return nullptr;
    }
    bool hasLocal(const std::string &name) const {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
            if (scope->contains(name))
                return true;
        return false;
    }
    ValueId integer(std::int64_t value) {
        Instruction instruction;
        instruction.op = Op::Constant;
        instruction.immediate = value;
        return emit(instruction);
    }
    ValueId runtime(int operation, std::vector<ValueId> arguments, Type type, Type element = Type::Any) {
        Instruction in;
        in.op = Op::RuntimeCall;
        in.immediate = operation;
        in.arguments = std::move(arguments);
        in.type = type;
        auto result = emit(in);
        if (type == Type::List)
            elements[result] = element;
        return result;
    }
    Type elementOf(ValueId id) {
        if (!elements.contains(id))
            throw std::runtime_error("Native List element type is not known; list parameters/ambiguous "
                                     "aliases require typed runtime metadata.");
        return elements.at(id);
    }
    ValueId convert(ValueId value, Type target) {
        const auto actual = function.valueTypes.at(value);
        if (actual == target)
            return value;
        if (actual == Type::Int && target == Type::Bool) {
            Instruction in;
            in.op = Op::ToBoolean;
            in.left = value;
            in.type = Type::Bool;
            return emit(in);
        }
        if (actual == Type::Bool && target == Type::Int)
            return value;
        if (actual == Type::Int && target == Type::Float) {
            Instruction in;
            in.op = Op::IntToFloat;
            in.left = value;
            in.type = Type::Float;
            return emit(in);
        }
        throw std::runtime_error("JM IR cannot convert " + typeName(actual) + " to " + typeName(target));
    }
    ValueId load(const std::string &name) {
        Instruction in;
        if (hasLocal(name)) {
            in.op = Op::Load;
            in.local = local(name);
            in.type = function.localTypes.at(in.local);
        } else if (auto found = global(name)) {
            in.op = Op::GlobalLoad;
            in.symbol = name;
            in.type = found->type;
        } else if (name == "PI" || name == "E") {
            in.op = Op::FloatConstant;
            in.type = Type::Float;
            in.floating = name == "PI" ? 3.14159265358979323846 : 2.71828182845904523536;
        } else
            throw std::runtime_error("JM IR cannot resolve variable '" + name + "'.");
        auto result = emit(in);
        if (in.type == Type::List && localElements.contains(in.local))
            elements[result] = localElements[in.local];
        return result;
    }
    void store(const std::string &name, ValueId value, bool initialization = false) {
        Instruction in;
        if (hasLocal(name)) {
            in.op = Op::Store;
            in.local = local(name);
            in.type = function.localTypes.at(in.local);
            if (constants[in.local] && !initialization)
                throw std::runtime_error("Cannot modify constant '" + name + "'.");
        } else if (auto found = global(name)) {
            in.op = Op::GlobalStore;
            in.symbol = name;
            in.type = found->type;
            if (found->constant && !initialization)
                throw std::runtime_error("Cannot modify global constant '" + name + "'.");
        } else
            throw std::runtime_error("Unknown assignment variable '" + name + "'.");
        in.left = convert(value, in.type);
        if (in.type == Type::List) {
            auto element = elementOf(value);
            if (localElements.contains(in.local) && localElements[in.local] != element)
                throw std::runtime_error("List assignment changes its native element type.");
            localElements[in.local] = element;
        }
        at(current).instructions.push_back(in);
    }
    ValueId defaultValue(Type type, Type element = Type::Any) {
        if (type == Type::String) {
            Instruction in;
            in.op = Op::StringConstant;
            in.type = Type::String;
            return emit(in);
        }
        if (type == Type::List)
            return runtime(JM_RT_LIST_CREATE,
                           {integer(static_cast<int>(element == Type::Any ? Type::Int : element))},
                           Type::List, element == Type::Any ? Type::Int : element);
        return convert(integer(0), type == Type::Void ? Type::Int : type);
    }
    ValueId expression(const ExpressionPtr &value, Type expectedElement = Type::Any) {
        if (!value)
            return integer(0);
        switch (value->kind) {
        case Expression::Kind::Literal:
            if (const auto *literalInt = std::get_if<std::int64_t>(&value->literal.data))
                return integer(*literalInt);
            if (const auto *number = std::get_if<double>(&value->literal.data)) {
                Instruction in;
                in.op = Op::FloatConstant;
                in.type = Type::Float;
                in.floating = *number;
                return emit(in);
            }
            if (const auto *boolean = std::get_if<bool>(&value->literal.data)) {
                Instruction in;
                in.op = Op::Constant;
                in.immediate = *boolean ? 1 : 0;
                in.type = Type::Bool;
                return emit(in);
            }
            if (auto text = std::get_if<std::string>(&value->literal.data)) {
                Instruction in;
                in.op = Op::StringConstant;
                in.type = Type::String;
                in.symbol = *text;
                return emit(in);
            }
            throw std::runtime_error("JM IR scalar backend does not support " + value->literal.typeName() +
                                     " literals. Use Interpreter for String/List/Map.");
        case Expression::Kind::Array: {
            std::vector<ValueId> values;
            Type element = expectedElement == Type::Any ? Type::Int : expectedElement;
            for (const auto &item : value->elements) {
                auto id = expression(item);
                values.push_back(id);
                if (expectedElement != Type::Any) {
                    if (function.valueTypes[id] != expectedElement &&
                        !(expectedElement == Type::Float && function.valueTypes[id] == Type::Int))
                        throw std::runtime_error("Typed native List element mismatch.");
                } else if (values.size() == 1)
                    element = function.valueTypes[id];
                else if (element != function.valueTypes[id]) {
                    throw std::runtime_error("Native List requires homogeneous elements. Use List<Float> to "
                                             "promote mixed Int/Float values explicitly.");
                }
            }
            if (element != Type::Int && element != Type::Float && element != Type::Bool &&
                element != Type::String)
                throw std::runtime_error("Native nested/boxed List elements are not supported.");
            auto result =
                runtime(JM_RT_LIST_CREATE, {integer(static_cast<int>(element))}, Type::List, element);
            for (auto id : values)
                runtime(JM_RT_LIST_PUSH, {result, convert(id, element), integer(static_cast<int>(element))},
                        Type::Void);
            return result;
        }
        case Expression::Kind::Index: {
            auto receiver = expression(value->left), index = convert(expression(value->right), Type::Int);
            auto type = function.valueTypes[receiver];
            if (type == Type::String)
                return runtime(JM_RT_INDEX, {receiver, index}, Type::String);
            if (type == Type::List) {
                auto element = elementOf(receiver);
                return runtime(JM_RT_LIST_GET, {receiver, index, integer(static_cast<int>(element))},
                               element);
            }
            throw std::runtime_error("Native indexing requires String/List.");
        }
        case Expression::Kind::Identifier:
            return load(value->text);
        case Expression::Kind::Unary: {
            const ValueId operand = expression(value->right);
            Instruction instruction;
            instruction.op = value->text == "~"   ? Op::BitNot
                             : value->text == "-" ? Op::Negate
                                                  : Op::LogicalNot;
            instruction.left = operand;
            instruction.type =
                instruction.op == Op::LogicalNot ? Type::Bool : function.valueTypes.at(operand);
            if (value->text == "+")
                return operand;
            if (value->text != "~" && value->text != "-" && value->text != "!" && value->text != "not")
                throw std::runtime_error("Unsupported unary operator in native x64.");
            return emit(std::move(instruction));
        }
        case Expression::Kind::Binary: {
            if (value->text == "**" || value->text == "^") {
                auto call = std::make_shared<Expression>();
                call->kind = Expression::Kind::Call;
                call->left = std::make_shared<Expression>();
                call->left->kind = Expression::Kind::Identifier;
                call->left->text = "builtin.math.pow";
                call->arguments = {{"", value->left}, {"", value->right}};
                return expression(call);
            }
            const ValueId left = expression(value->left);
            if (value->text == "and" || value->text == "&&" || value->text == "or" || value->text == "||") {
                const bool isAnd = value->text == "and" || value->text == "&&";
                const auto resultSlot = declare("$logic" + std::to_string(nextValue), Type::Bool);
                const auto rhsBlock = block("logic.rhs"), shortBlock = block("logic.short"),
                           mergeBlock = block("logic.end");
                at(current).terminator = {Terminator::Kind::ConditionalBranch, left,
                                          isAnd ? rhsBlock : shortBlock, isAnd ? shortBlock : rhsBlock};
                select(shortBlock);
                Instruction shortConstant;
                shortConstant.op = Op::Constant;
                shortConstant.type = Type::Bool;
                shortConstant.immediate = isAnd ? 0 : 1;
                const auto shortValue = emit(shortConstant);
                Instruction shortStore;
                shortStore.op = Op::Store;
                shortStore.local = resultSlot;
                shortStore.left = shortValue;
                shortStore.type = Type::Bool;
                at(current).instructions.push_back(std::move(shortStore));
                at(current).terminator = {Terminator::Kind::Branch, 0, mergeBlock, 0};
                select(rhsBlock);
                const auto rawRight = expression(value->right);
                Instruction truthValue;
                truthValue.op = Op::ToBoolean;
                truthValue.left = rawRight;
                truthValue.type = Type::Bool;
                const auto rightValue = emit(std::move(truthValue));
                Instruction rightStore;
                rightStore.op = Op::Store;
                rightStore.local = resultSlot;
                rightStore.left = rightValue;
                rightStore.type = Type::Bool;
                at(current).instructions.push_back(std::move(rightStore));
                at(current).terminator = {Terminator::Kind::Branch, 0, mergeBlock, 0};
                select(mergeBlock);
                Instruction result;
                result.op = Op::Load;
                result.local = resultSlot;
                result.type = Type::Bool;
                return emit(std::move(result));
            }
            const ValueId right = expression(value->right);
            static const std::unordered_map<std::string, Op> operations{
                {"&", Op::BitAnd},     {"|", Op::BitOr},    {"<<", Op::ShiftLeft},   {">>", Op::ShiftRight},
                {"+", Op::Add},        {"-", Op::Subtract}, {"*", Op::Multiply},     {"/", Op::Divide},
                {"%", Op::Modulo},     {"==", Op::Equal},   {"!=", Op::NotEqual},    {"<", Op::Less},
                {"<=", Op::LessEqual}, {">", Op::Greater},  {">=", Op::GreaterEqual}};
            const auto found = operations.find(value->text);
            if (found == operations.end())
                throw std::runtime_error("Unsupported native binary operator '" + value->text + "'.");
            Instruction instruction;
            instruction.op = found->second;
            const auto leftType = function.valueTypes.at(left), rightType = function.valueTypes.at(right);
            if (leftType == Type::List && rightType == Type::List &&
                (value->text == "==" || value->text == "!=")) {
                auto result = runtime(JM_RT_LIST_EQUAL, {left, right}, Type::Bool);
                if (value->text == "==")
                    return result;
                Instruction in;
                in.op = Op::LogicalNot;
                in.left = result;
                in.type = Type::Bool;
                return emit(in);
            }
            if (leftType == Type::String && rightType == Type::String) {
                if (value->text == "+")
                    return runtime(JM_RT_CONCAT, {left, right}, Type::String);
                auto comparison = runtime(
                    value->text == "==" || value->text == "!=" ? JM_RT_EQUAL : JM_RT_COMPARE, {left, right},
                    value->text == "==" || value->text == "!=" ? Type::Bool : Type::Int);
                if (value->text == "==")
                    return comparison;
                Instruction in;
                in.op = value->text == "!=" ? Op::LogicalNot : found->second;
                in.left = comparison;
                in.right = integer(0);
                in.type = Type::Bool;
                return emit(in);
            }
            if ((value->text == "==" || value->text == "!=") && leftType != rightType &&
                !((leftType == Type::Int && rightType == Type::Float) ||
                  (leftType == Type::Float && rightType == Type::Int))) {
                Instruction result;
                result.op = Op::Constant;
                result.type = Type::Bool;
                result.immediate = value->text == "!=" ? 1 : 0;
                return emit(result);
            }
            const auto operandType = leftType == Type::Bool && rightType == Type::Bool     ? Type::Bool
                                     : leftType == Type::Float || rightType == Type::Float ? Type::Float
                                                                                           : Type::Int;
            instruction.left = convert(left, operandType);
            instruction.right = convert(right, operandType);
            instruction.type =
                found->second >= Op::Equal && found->second <= Op::GreaterEqual ? Type::Bool : operandType;
            return emit(std::move(instruction));
        }
        case Expression::Kind::Member: {
            if (value->left && value->left->kind == Expression::Kind::Identifier &&
                value->left->text == "math" && !hasLocal("math") && !global("math") &&
                (value->text == "PI" || value->text == "E")) {
                Instruction in;
                in.op = Op::FloatConstant;
                in.type = Type::Float;
                in.floating = value->text == "PI" ? 3.14159265358979323846 : 2.71828182845904523536;
                return emit(in);
            }
            if (value->text == "length") {
                auto receiver = expression(value->left);
                auto type = function.valueTypes[receiver];
                if (type == Type::List || type == Type::String)
                    return runtime(JM_RT_LENGTH, {receiver, integer(0), integer(static_cast<int>(type))},
                                   Type::Int);
            }
            if (!value->builtinSymbolId)
                throw std::runtime_error("Member property has no stable native symbol.");
            auto call = std::make_shared<Expression>();
            call->kind = Expression::Kind::Call;
            call->left = value;
            call->builtinSymbolId = value->builtinSymbolId;
            call->builtinSymbolName = value->builtinSymbolName;
            return expression(call);
        }
        case Expression::Kind::Call: {
            if (value->left && value->left->kind == Expression::Kind::Member) {
                const auto &member = *value->left;
                if (member.left && (member.left->kind != Expression::Kind::Identifier ||
                                    hasLocal(member.left->text) || global(member.left->text))) {
                    auto receiver = expression(member.left);
                    auto type = function.valueTypes[receiver];
                    std::vector<ValueId> args;
                    for (const auto &arg : value->arguments) {
                        if (!arg.name.empty())
                            throw std::runtime_error(
                                "Collection/string methods require positional arguments.");
                        args.push_back(expression(arg.value));
                    }
                    auto arity = [&](size_t count) {
                        if (args.size() != count)
                            throw std::runtime_error("Method argument count mismatch: " + member.text);
                    };
                    if (type == Type::String) {
                        const std::unordered_map<std::string, std::pair<int, Type>> methods{
                            {"substring", {JM_RT_SUBSTRING, Type::String}},
                            {"contains", {JM_RT_CONTAINS, Type::Bool}},
                            {"startsWith", {JM_RT_STARTS_WITH, Type::Bool}},
                            {"endsWith", {JM_RT_ENDS_WITH, Type::Bool}},
                            {"find", {JM_RT_FIND, Type::Int}},
                            {"replace", {JM_RT_REPLACE, Type::String}},
                            {"split", {JM_RT_SPLIT, Type::List}},
                            {"trim", {JM_RT_TRIM, Type::String}},
                            {"upper", {JM_RT_UPPER, Type::String}},
                            {"lower", {JM_RT_LOWER, Type::String}},
                            {"codepointLength", {JM_RT_CODEPOINT_LENGTH, Type::Int}}};
                        if (auto found = methods.find(member.text); found != methods.end()) {
                            auto [op, result] = found->second;
                            arity(op == JM_RT_SUBSTRING || op == JM_RT_REPLACE ? 2
                                  : ((op >= JM_RT_TRIM && op <= JM_RT_LOWER) || op == JM_RT_CODEPOINT_LENGTH)
                                      ? 0
                                      : 1);
                            for (auto id : args)
                                if (function.valueTypes[id] !=
                                    (op == JM_RT_SUBSTRING ? Type::Int : Type::String))
                                    throw std::runtime_error("String method argument type mismatch.");
                            args.insert(args.begin(), receiver);
                            return runtime(op, args, result, result == Type::List ? Type::String : Type::Any);
                        }
                    }
                    if (type == Type::List) {
                        auto element = elementOf(receiver);
                        auto method = member.text;
                        if (method == "push" || method == "append" || method == "contains" ||
                            method == "indexOf") {
                            arity(1);
                            int op = method == "contains"  ? JM_RT_LIST_CONTAINS
                                     : method == "indexOf" ? JM_RT_LIST_INDEX_OF
                                                           : JM_RT_LIST_PUSH;
                            return runtime(
                                op, {receiver, convert(args[0], element), integer(static_cast<int>(element))},
                                method == "contains"  ? Type::Bool
                                : method == "indexOf" ? Type::Int
                                                      : Type::Void);
                        }
                        if (method == "pop") {
                            arity(0);
                            return runtime(JM_RT_LIST_POP,
                                           {receiver, integer(0), integer(static_cast<int>(element))},
                                           element);
                        }
                        if (method == "clear" || method == "reverse" || method == "sort") {
                            arity(0);
                            return runtime(method == "clear"     ? JM_RT_LIST_CLEAR
                                           : method == "reverse" ? JM_RT_LIST_REVERSE
                                                                 : JM_RT_LIST_SORT,
                                           {receiver}, Type::Void);
                        }
                        if (method == "insert") {
                            arity(2);
                            return runtime(JM_RT_LIST_INSERT,
                                           {receiver, convert(args[0], Type::Int), convert(args[1], element)},
                                           Type::Void);
                        }
                        if (method == "removeAt") {
                            arity(1);
                            return runtime(JM_RT_LIST_REMOVE_AT, {receiver, convert(args[0], Type::Int)},
                                           element);
                        }
                    }
                    throw std::runtime_error("Native method is not supported: " + member.text);
                }
            }
            if (value->left && value->left->kind == Expression::Kind::Identifier &&
                !declarations.contains(value->left->text)) {
                const auto &name = value->left->text;
                if (name == "len" || name == "length") {
                    if (value->arguments.size() != 1)
                        throw std::runtime_error("length requires one argument.");
                    auto receiver = expression(value->arguments[0].value);
                    auto type = function.valueTypes[receiver];
                    if (type != Type::String && type != Type::List)
                        throw std::runtime_error("Native length requires String/List.");
                    return runtime(JM_RT_LENGTH, {receiver, integer(0), integer(static_cast<int>(type))},
                                   Type::Int);
                }
                if (name == "string") {
                    if (value->arguments.size() != 1)
                        throw std::runtime_error("string requires one argument.");
                    auto id = expression(value->arguments[0].value);
                    return runtime(JM_RT_TO_STRING, {id, integer(static_cast<int>(function.valueTypes[id]))},
                                   Type::String);
                }
                if (name == "print" || name == "println") {
                    auto text = defaultValue(Type::String);
                    for (size_t i = 0; i < value->arguments.size(); ++i) {
                        if (!value->arguments[i].name.empty())
                            throw std::runtime_error("Print requires positional arguments.");
                        if (i) {
                            Instruction space;
                            space.op = Op::StringConstant;
                            space.type = Type::String;
                            space.symbol = " ";
                            text = runtime(JM_RT_CONCAT, {text, emit(space)}, Type::String);
                        }
                        auto id = expression(value->arguments[i].value);
                        auto item =
                            runtime(JM_RT_TO_STRING, {id, integer(static_cast<int>(function.valueTypes[id]))},
                                    Type::String);
                        text = runtime(JM_RT_CONCAT, {text, item}, Type::String);
                    }
                    return runtime(name == "print" ? JM_RT_PRINT : JM_RT_PRINTLN,
                                   {text, integer(static_cast<int>(Type::String))}, Type::Void);
                }
                if (name == "int" || name == "float") {
                    if (value->arguments.size() != 1)
                        throw std::runtime_error("Conversion requires one argument.");
                    auto id = expression(value->arguments[0].value);
                    auto actual = function.valueTypes[id];
                    if (actual == Type::String)
                        return runtime(name == "int" ? JM_RT_PARSE_INT : JM_RT_PARSE_FLOAT, {id},
                                       name == "int" ? Type::Int : Type::Float);
                    if (name == "float")
                        return convert(id, Type::Float);
                    if (actual == Type::Int)
                        return id;
                    if (actual != Type::Float)
                        throw std::runtime_error("Native Int conversion requires Float or Int.");
                    Instruction in;
                    in.op = Op::FloatToInt;
                    in.left = id;
                    in.type = Type::Int;
                    return emit(in);
                }
                if (name == "bitXor") {
                    if (value->arguments.size() != 2)
                        throw std::runtime_error("bitXor requires two Int arguments.");
                    Instruction in;
                    in.op = Op::BitXor;
                    in.left = expression(value->arguments[0].value);
                    in.right = expression(value->arguments[1].value);
                    return emit(in);
                }
            }

            if (!value->left || (value->left->kind != Expression::Kind::Identifier &&
                                 value->left->kind != Expression::Kind::Member))
                throw std::runtime_error(
                    "Native calls currently require a named function or registered member function.");
            Instruction instruction;
            instruction.op = Op::Call;
            if (value->left->kind == Expression::Kind::Member) {
                if (value->builtinSymbolId == 0 || value->builtinSymbolName.empty())
                    throw std::runtime_error("Native member call has no stable builtin symbol.");
                instruction.symbol = value->builtinSymbolName;
                instruction.symbolId = value->builtinSymbolId;
            } else
                instruction.symbol = value->left->text;
            if (!instruction.symbolId)
                instruction.symbolId = stableBuiltinSymbolId(instruction.symbol);
            auto metadata = registry && !declarations.contains(instruction.symbol)
                                ? registry->metadata(instruction.symbolId)
                                : nullptr;
            if (metadata) {
                instruction.type = metadata->returnType;
                if (value->arguments.size() != metadata->parameterTypes.size())
                    throw std::runtime_error("Typed FFI argument count mismatch: " + instruction.symbol);
            }
            const auto found = declarations.find(instruction.symbol);
            const auto standard =
                found == declarations.end() ? standardFunction(instruction.symbol) : std::nullopt;
            if (standard) {
                if (value->arguments.size() < standard->minimumArity ||
                    value->arguments.size() > standard->maximumArity)
                    throw std::runtime_error("Standard library argument count mismatch: " +
                                             instruction.symbol);
                instruction.symbol = standard->symbol;
                instruction.symbolId = stableBuiltinSymbolId(instruction.symbol);
                instruction.type = standard->returnType;
            }
            if (found != declarations.end()) {
                instruction.type =
                    found->second->returnType == Type::Any ? Type::Int : found->second->returnType;
                if (value->arguments.size() != found->second->parameters.size())
                    throw std::runtime_error("Function '" + instruction.symbol +
                                             "' argument count mismatch.");
            }
            for (std::size_t index = 0; index < value->arguments.size(); ++index) {
                const auto &argument = value->arguments[index];
                auto result =
                    expression(argument.value, found != declarations.end() &&
                                                       index < found->second->parameterElementTypes.size()
                                                   ? found->second->parameterElementTypes[index]
                                                   : Type::Any);
                if (found != declarations.end()) {
                    if (!argument.name.empty() && argument.name != found->second->parameters[index])
                        throw std::runtime_error("Named argument order must match function parameters.");
                    auto type = index < found->second->parameterTypes.size()
                                    ? found->second->parameterTypes[index]
                                    : Type::Any;
                    result = convert(result, type == Type::Any ? Type::Int : type);
                    if (type == Type::List && index < found->second->parameterElementTypes.size() &&
                        found->second->parameterElementTypes[index] != Type::Any &&
                        elementOf(result) != found->second->parameterElementTypes[index])
                        throw std::runtime_error("Typed List parameter element mismatch.");
                }
                if (metadata) {
                    if (!argument.name.empty() && argument.name != metadata->parameterNames[index])
                        throw std::runtime_error("Typed FFI named arguments must match metadata order.");
                    result = convert(result, metadata->parameterTypes[index]);
                }
                instruction.arguments.push_back(result);
                instruction.argumentNames.push_back(argument.name);
            }
            if (standard && runtimeStandardOperation(instruction.symbol)) {
                for (const auto &name : instruction.argumentNames)
                    if (!name.empty())
                        throw std::runtime_error("Standard library calls require positional arguments.");
                auto operation = runtimeStandardOperation(instruction.symbol);
                auto type = standard->numericArguments ? Type::Float : Type::Int;
                for (auto &id : instruction.arguments)
                    id = convert(id, type);
                return runtime(operation, instruction.arguments, standard->returnType);
            }
            if (standard) {
                for (const auto &name : instruction.argumentNames)
                    if (!name.empty())
                        throw std::runtime_error(
                            "Standard library calls currently require positional arguments.");
                auto type = standard->returnType;
                if (type == Type::Any) {
                    type = Type::Int;
                    for (auto id : instruction.arguments)
                        if (function.valueTypes[id] == Type::Float)
                            type = Type::Float;
                }
                instruction.type = type;
                for (auto &id : instruction.arguments)
                    id = convert(id, type);
            }
            auto id = emit(std::move(instruction));
            if (found != declarations.end() && found->second->returnType == Type::List)
                elements[id] = found->second->returnElementType;
            return id;
        }
        default:
            throw std::runtime_error("JM IR does not support Map or this expression yet.");
        }
    }
    void statements(const StatementList &list) {
        for (const Statement &statement : list) {
            if (at(current).terminator.kind != Terminator::Kind::None)
                return;
            switch (statement.kind) {
            case Statement::Kind::Enum:
            case Statement::Kind::Struct:
                throw std::runtime_error(
                    "Native data declarations require an unsupported managed data runtime.");
            case Statement::Kind::Variable: {
                if (!statement.expression && statement.declaredType == Type::Any)
                    throw std::runtime_error("Native uninitialized local requires an explicit scalar type.");
                const auto value = statement.expression
                                       ? expression(statement.expression, statement.elementType)
                                       : defaultValue(statement.declaredType, statement.elementType);
                const auto type = statement.declaredType == Type::Any ? function.valueTypes.at(value)
                                                                      : statement.declaredType;
                if (type != Type::String && type != Type::List && type != Type::Int && type != Type::Bool &&
                    type != Type::Float)
                    throw std::runtime_error("Native local '" + statement.name + "' has unsupported type " +
                                             typeName(type) + "; use Interpreter.");
                declare(statement.name, type, statement.constant);
                store(statement.name, value, true);
                break;
            }
            case Statement::Kind::Assignment: {
                if (statement.target && statement.target->kind == Expression::Kind::Index) {
                    if (statement.operation != "=")
                        throw std::runtime_error("Native compound indexed assignment is not supported yet.");
                    auto receiver = expression(statement.target->left);
                    if (function.valueTypes[receiver] != Type::List)
                        throw std::runtime_error("Native indexed assignment requires List.");
                    runtime(JM_RT_LIST_SET,
                            {receiver, convert(expression(statement.target->right), Type::Int),
                             convert(expression(statement.expression), elementOf(receiver))},
                            Type::Void);
                    break;
                }
                if (!statement.target || statement.target->kind != Expression::Kind::Identifier)
                    throw std::runtime_error(
                        "Scalar JM IR assignment requires a variable; list writes use Interpreter.");
                auto result = expression(statement.expression);
                if (statement.operation != "=") {
                    auto old = load(statement.target->text);
                    Instruction in;
                    const auto type = function.valueTypes.at(old);
                    in.left = old;
                    in.right = convert(result, type);
                    in.type = type;
                    if (type == Type::String) {
                        if (statement.operation != "+=")
                            throw std::runtime_error("String compound assignment requires +=.");
                        result = runtime(JM_RT_CONCAT, {old, convert(result, Type::String)}, Type::String);
                        store(statement.target->text, result);
                        break;
                    }
                    in.op = statement.operation == "+="   ? Op::Add
                            : statement.operation == "-=" ? Op::Subtract
                            : statement.operation == "*=" ? Op::Multiply
                            : statement.operation == "%=" ? Op::Modulo
                                                          : Op::Divide;
                    result = emit(in);
                }
                store(statement.target->text, result, function.name == "__jm_init");
                break;
            }
            case Statement::Kind::Expression:
                (void)expression(statement.expression);
                break;
            case Statement::Kind::Return: {
                const ValueId result = expression(statement.expression, function.returnElementType);
                if (function.returnType == Type::List) {
                    auto element = elementOf(result);
                    if (function.returnElementType != Type::Any && function.returnElementType != element)
                        throw std::runtime_error("Typed List return element mismatch.");
                    function.returnElementType = element;
                }
                const auto converted =
                    function.returnType == Type::Void ? result : convert(result, function.returnType);
                at(current).terminator = {Terminator::Kind::Return, converted, 0, 0};
                break;
            }
            case Statement::Kind::If: {
                const ValueId condition = expression(statement.expression);
                const auto thenBlock = block("if.then"), elseBlock = block("if.else"),
                           mergeBlock = block("if.end");
                at(current).terminator = {Terminator::Kind::ConditionalBranch, condition, thenBlock,
                                          elseBlock};
                scopes.emplace_back();
                select(thenBlock);
                statements(statement.body);
                if (at(current).terminator.kind == Terminator::Kind::None)
                    at(current).terminator = {Terminator::Kind::Branch, 0, mergeBlock, 0};
                scopes.pop_back();
                scopes.emplace_back();
                select(elseBlock);
                statements(statement.alternative);
                if (at(current).terminator.kind == Terminator::Kind::None)
                    at(current).terminator = {Terminator::Kind::Branch, 0, mergeBlock, 0};
                scopes.pop_back();
                select(mergeBlock);
                break;
            }
            case Statement::Kind::While: {
                const auto conditionBlock = block("while.cond"), bodyBlock = block("while.body"),
                           endBlock = block("while.end");
                at(current).terminator = {Terminator::Kind::Branch, 0, conditionBlock, 0};
                select(conditionBlock);
                const auto condition = expression(statement.expression);
                at(current).terminator = {Terminator::Kind::ConditionalBranch, condition, bodyBlock,
                                          endBlock};
                loops.emplace_back(endBlock, conditionBlock);
                scopes.emplace_back();
                select(bodyBlock);
                statements(statement.body);
                if (at(current).terminator.kind == Terminator::Kind::None)
                    at(current).terminator = {Terminator::Kind::Branch, 0, conditionBlock, 0};
                scopes.pop_back();
                loops.pop_back();
                select(endBlock);
                break;
            }
            case Statement::Kind::Event:
            case Statement::Kind::Function:
                throw std::runtime_error("Nested function declarations are not supported by native x64.");
            case Statement::Kind::ForRange: {
                const auto start = convert(expression(statement.expression), Type::Int),
                           end = convert(expression(statement.rangeEnd), Type::Int);
                scopes.emplace_back();
                declare(statement.name);
                store(statement.name, start, true);
                const auto limitName = "$limit" + std::to_string(nextValue);
                declare(limitName);
                store(limitName, end, true);
                const auto conditionBlock = block("for.cond"), bodyBlock = block("for.body"),
                           stepBlock = block("for.step"), endBlock = block("for.end");
                at(current).terminator = {Terminator::Kind::Branch, 0, conditionBlock, 0};
                select(conditionBlock);
                Instruction compare;
                compare.op = Op::Less;
                compare.left = load(statement.name);
                compare.right = load(limitName);
                compare.type = Type::Bool;
                const auto condition = emit(compare);
                at(current).terminator = {Terminator::Kind::ConditionalBranch, condition, bodyBlock,
                                          endBlock};
                loops.emplace_back(endBlock, stepBlock);
                select(bodyBlock);
                scopes.emplace_back();
                statements(statement.body);
                scopes.pop_back();
                if (at(current).terminator.kind == Terminator::Kind::None)
                    at(current).terminator = {Terminator::Kind::Branch, 0, stepBlock, 0};
                loops.pop_back();
                select(stepBlock);
                Instruction increment;
                increment.op = Op::Add;
                increment.left = load(statement.name);
                increment.right = integer(1);
                store(statement.name, emit(increment));
                at(current).terminator = {Terminator::Kind::Branch, 0, conditionBlock, 0};
                scopes.pop_back();
                select(endBlock);
                break;
            }
            case Statement::Kind::ForEach: {
                auto source = expression(statement.expression);
                if (function.valueTypes[source] != Type::List)
                    throw std::runtime_error("Native foreach currently requires homogeneous List.");
                auto element = elementOf(source);
                auto snapshot = runtime(JM_RT_LIST_CLONE, {source}, Type::List, element);
                auto end = runtime(JM_RT_LENGTH,
                                   {snapshot, integer(0), integer(static_cast<int>(Type::List))}, Type::Int);
                scopes.emplace_back();
                declare(statement.name, element);
                auto counter = "$foreach" + std::to_string(nextValue);
                declare(counter);
                store(counter, integer(0), true);
                auto conditionBlock = block("foreach.cond"), bodyBlock = block("foreach.body"),
                     stepBlock = block("foreach.step"), endBlock = block("foreach.end");
                at(current).terminator = {Terminator::Kind::Branch, 0, conditionBlock, 0};
                select(conditionBlock);
                Instruction compare;
                compare.op = Op::Less;
                compare.left = load(counter);
                compare.right = end;
                compare.type = Type::Bool;
                auto condition = emit(compare);
                at(current).terminator = {Terminator::Kind::ConditionalBranch, condition, bodyBlock,
                                          endBlock};
                loops.emplace_back(endBlock, stepBlock);
                select(bodyBlock);
                store(statement.name,
                      runtime(JM_RT_LIST_GET, {snapshot, load(counter), integer(static_cast<int>(element))},
                              element),
                      true);
                scopes.emplace_back();
                statements(statement.body);
                scopes.pop_back();
                if (at(current).terminator.kind == Terminator::Kind::None)
                    at(current).terminator = {Terminator::Kind::Branch, 0, stepBlock, 0};
                loops.pop_back();
                select(stepBlock);
                Instruction increment;
                increment.op = Op::Add;
                increment.left = load(counter);
                increment.right = integer(1);
                store(counter, emit(increment));
                at(current).terminator = {Terminator::Kind::Branch, 0, conditionBlock, 0};
                scopes.pop_back();
                select(endBlock);
                break;
            }
            case Statement::Kind::Break:
            case Statement::Kind::Continue:
                if (loops.empty())
                    throw std::runtime_error("break/continue outside a loop.");
                at(current).terminator = {
                    Terminator::Kind::Branch, 0,
                    statement.kind == Statement::Kind::Break ? loops.back().first : loops.back().second, 0};
                break;
            case Statement::Kind::Import:
                break;
            }
        }
    }
    Function build(const Statement &declaration) {
        function.name = declaration.name;
        function.returnElementType = declaration.returnElementType;
        function.returnType = declaration.returnType == Type::Any ? Type::Int : declaration.returnType;
        function.parameterCount = static_cast<std::uint32_t>(declaration.parameters.size());
        scopes.emplace_back();
        std::function<std::size_t(const StatementList &)> countBlocks = [&](const StatementList &list) {
            std::size_t count = 1;
            for (const Statement &item : list) {
                if (item.kind == Statement::Kind::If || item.kind == Statement::Kind::While)
                    count += 3;
                count += countBlocks(item.body);
                count += countBlocks(item.alternative);
            }
            return count;
        };
        function.blocks.reserve(countBlocks(declaration.body));
        for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
            const auto type =
                i < declaration.parameterTypes.size() && declaration.parameterTypes[i] != Type::Any
                    ? declaration.parameterTypes[i]
                    : Type::Int;
            function.parameterTypes.push_back(type);
            auto slot = declare(declaration.parameters[i], type);
            auto element = i < declaration.parameterElementTypes.size() ? declaration.parameterElementTypes[i]
                                                                        : Type::Any;
            function.parameterElementTypes.push_back(element);
            if (type == Type::List) {
                if (element == Type::Any)
                    throw std::runtime_error("Native List parameters require List<T> annotations.");
                localElements[slot] = element;
            }
        }
        const auto entry = block("entry");
        select(entry);
        statements(declaration.body);
        if (at(current).terminator.kind == Terminator::Kind::None) {
            const auto result = defaultValue(function.returnType, function.returnElementType);
            at(current).terminator = {Terminator::Kind::Return, result, 0, 0};
        }
        function.valueCount = nextValue;
        return std::move(function);
    }
};

} // namespace

bool lower(const Program &program, Module &output, LoweringDiagnostic &diagnostic,
           const NativeFunctionRegistry *registry) {
    try {
        std::vector<Diagnostic> errors;
        if (!check(program, errors))
            throw std::runtime_error(errors.front().code + ": " + errors.front().message);
        Module result;
        std::unordered_map<std::string, const Statement *> declarations;
        std::unordered_map<std::string, const Statement *> userFunctions;
        std::unordered_set<std::string> allGlobals, initialized;
        for (const auto &item : program.statements) {
            if (item.kind == Statement::Kind::Variable)
                allGlobals.insert(item.name);
            if (item.kind == Statement::Kind::Function) {
                if (item.name.rfind("__jm_", 0) == 0)
                    throw std::runtime_error(
                        "Names beginning __jm_ are reserved for runtime initialization/events.");
                userFunctions[item.name] = &item;
            }
        }
        std::function<void(const ExpressionPtr &, std::unordered_set<std::string> &)> dependencies;
        std::function<void(const StatementList &, std::unordered_set<std::string> &)> functionDependencies;
        dependencies = [&](const ExpressionPtr &value, std::unordered_set<std::string> &visited) {
            if (!value)
                return;
            if (value->kind == Expression::Kind::Identifier && allGlobals.contains(value->text) &&
                !initialized.contains(value->text))
                throw std::runtime_error("Global initializer reads uninitialized global '" + value->text +
                                         "'. Initialize globals in dependency order.");
            if (value->kind == Expression::Kind::Call && value->left &&
                value->left->kind == Expression::Kind::Identifier) {
                const auto &name = value->left->text;
                if (userFunctions.contains(name) && visited.insert(name).second)
                    functionDependencies(userFunctions.at(name)->body, visited);
            }
            dependencies(value->left, visited);
            dependencies(value->right, visited);
            for (const auto &item : value->elements)
                dependencies(item, visited);
            for (const auto &item : value->entries)
                dependencies(item.second, visited);
            for (const auto &item : value->arguments)
                dependencies(item.value, visited);
        };
        functionDependencies = [&](const StatementList &list, std::unordered_set<std::string> &visited) {
            for (const auto &item : list) {
                dependencies(item.expression, visited);
                dependencies(item.target, visited);
                dependencies(item.rangeEnd, visited);
                functionDependencies(item.body, visited);
                functionDependencies(item.alternative, visited);
            }
        };
        Statement initializer;
        initializer.kind = Statement::Kind::Function;
        initializer.name = "__jm_init";
        initializer.returnType = Type::Void;
        std::vector<Statement> normalized;
        normalized.reserve(program.statements.size());
        for (const auto &item : program.statements) {
            if (item.kind == Statement::Kind::Event) {
                auto handler = item;
                handler.kind = Statement::Kind::Function;
                handler.name = "__jm_event_" + std::to_string(stableBuiltinSymbolId(item.name)) + "_" +
                               std::to_string(normalized.size());
                handler.returnType = Type::Void;
                result.events.push_back({item.name, handler.name});
                normalized.push_back(std::move(handler));
            } else
                normalized.push_back(item);
        }
        std::unordered_map<std::string, Type> variables, signatures;
        std::unordered_set<std::string> inferred;
        if (registry)
            for (const auto &info : registry->allMetadata())
                signatures[info.symbol] = info.returnType;
        for (const auto &item : normalized) {
            if (item.kind == Statement::Kind::Variable)
                variables[item.name] = item.declaredType == Type::Any
                                           ? inferredExpression(item.expression, variables, signatures)
                                           : item.declaredType;
            if (item.kind == Statement::Kind::Function) {
                signatures[item.name] = item.returnType == Type::Any ? Type::Int : item.returnType;
                if (item.returnType == Type::Any)
                    inferred.insert(item.name);
            }
        }
        for (std::size_t iteration = 0; iteration <= normalized.size(); ++iteration)
            for (auto &item : normalized)
                if (item.kind == Statement::Kind::Function && inferred.contains(item.name)) {
                    auto locals = variables;
                    for (std::size_t i = 0; i < item.parameters.size(); ++i)
                        locals[item.parameters[i]] =
                            i < item.parameterTypes.size() && item.parameterTypes[i] != Type::Any
                                ? item.parameterTypes[i]
                                : Type::Int;
                    item.returnType = inferredReturn(item.body, locals, signatures);
                    signatures[item.name] = item.returnType;
                }
        for (const auto &item : normalized) {
            if (item.kind == Statement::Kind::Function) {
                if (item.returnType == Type::Any)
                    throw std::runtime_error("Native function has ambiguous return types: " + item.name);
                if (item.returnType != Type::Void && !guaranteedReturn(item.body))
                    throw std::runtime_error("Native function may finish without returning a value: " +
                                             item.name);
            }
        }
        for (const auto &item : normalized) {
            if (item.kind == Statement::Kind::Function) {
                if (item.name.rfind("__jm_", 0) == 0 && item.name.rfind("__jm_event_", 0) != 0)
                    throw std::runtime_error(
                        "Names beginning __jm_ are reserved for runtime initialization.");
                declarations[item.name] = &item;
            } else if (item.kind == Statement::Kind::Variable) {
                std::unordered_set<std::string> visited;
                dependencies(item.expression, visited);
                auto type = item.declaredType;
                if (type == Type::Any) {
                    if (item.expression && item.expression->kind == Expression::Kind::Literal)
                        type = item.expression->literal.type();
                    else
                        type = variables.at(item.name);
                }
                if (type != Type::String && type != Type::Int && type != Type::Float && type != Type::Bool)
                    throw std::runtime_error("Global " + item.name +
                                             " uses a non-scalar type; use Interpreter.");
                result.globals.push_back({item.name, type, item.constant});
                Statement assignment;
                assignment.kind = Statement::Kind::Assignment;
                assignment.operation = "=";
                assignment.target = std::make_shared<Expression>();
                assignment.target->kind = Expression::Kind::Identifier;
                assignment.target->text = item.name;
                assignment.expression = item.expression;
                initializer.body.push_back(assignment);
                initialized.insert(item.name);
            } else if (item.kind == Statement::Kind::Import) {
                if (item.fileImport)
                    throw std::runtime_error("Resolve file imports with loadModules before lowering.");
                result.imports.push_back(item.name);
            } else if (item.kind == Statement::Kind::Enum || item.kind == Statement::Kind::Struct)
                throw std::runtime_error(
                    "JM6002: Native Enum/Struct runtime is unsupported; use Interpreter.");
            else
                throw std::runtime_error(
                    "Native modules require functions/globals/imports; top-level actions use Interpreter.");
        }
        for (const auto &item : normalized)
            if (item.kind == Statement::Kind::Function) {
                Lowerer lowerer;
                lowerer.module = &result;
                lowerer.registry = registry;
                lowerer.declarations = declarations;
                result.functions.push_back(lowerer.build(item));
            }
        if (!result.globals.empty()) {
            Lowerer lowerer;
            lowerer.module = &result;
            lowerer.registry = registry;
            lowerer.declarations = declarations;
            result.functions.push_back(lowerer.build(initializer));
        }
        if (result.functions.empty())
            throw std::runtime_error("Native module has no functions.");
        if (!verify(result, diagnostic))
            return false;
        output = std::move(result);
        diagnostic.message.clear();
        return true;
    } catch (const std::exception &error) {
        diagnostic.message = error.what();
        return false;
    }
}

std::string format(const Module &module) {
    std::ostringstream out;
    for (const auto &identity : module.imports)
        out << "import " << identity << "\n";
    for (const auto &event : module.events)
        out << "event " << event.event << " -> @" << event.function << "\n";
    for (const auto &global : module.globals)
        out << "global @" << global.name << " : " << typeName(global.type) << "\n";
    for (const Function &function : module.functions) {
        out << "func " << function.name << "(" << function.parameterCount << " parameters) -> "
            << typeName(function.returnType) << " {\n";
        for (const BasicBlock &block : function.blocks) {
            out << block.name << ":\n";
            for (const Instruction &instruction : block.instructions) {
                out << "  ";
                if (instruction.op != Op::Store && instruction.op != Op::GlobalStore)
                    out << '%' << instruction.result << " = ";
                switch (instruction.op) {
                case Op::StringConstant:
                    out << "const.string " << std::quoted(instruction.symbol);
                    break;
                case Op::RuntimeCall:
                    out << "runtime." << instruction.immediate;
                    for (auto id : instruction.arguments)
                        out << " %" << id;
                    break;
                case Op::FloatToInt:
                    out << "convert.f64.i64 %" << instruction.left;
                    break;
                case Op::BitNot:
                    out << "bit.not %" << instruction.left;
                    break;
                case Op::BitAnd:
                case Op::BitOr:
                case Op::BitXor:
                case Op::ShiftLeft:
                case Op::ShiftRight:
                    out << "bit.op." << static_cast<int>(instruction.op) << " %" << instruction.left << ", %"
                        << instruction.right;
                    break;
                case Op::FloatConstant:
                    out << "const.f64 " << instruction.floating;
                    break;
                case Op::IntToFloat:
                    out << "convert.i64.f64 %" << instruction.left;
                    break;
                case Op::GlobalLoad:
                    out << "load.global @" << instruction.symbol;
                    break;
                case Op::GlobalStore:
                    out << "store.global @" << instruction.symbol << ", %" << instruction.left;
                    break;
                case Op::Constant:
                    out << "const.i64 " << instruction.immediate;
                    break;
                case Op::Load:
                    out << "load.local " << instruction.local;
                    break;
                case Op::Store:
                    out << "store.local " << instruction.local << ", %" << instruction.left;
                    break;
                case Op::Call:
                    out << "call @" << instruction.symbol << "(";
                    for (std::size_t i = 0; i < instruction.arguments.size(); ++i) {
                        if (i)
                            out << ", ";
                        out << '%' << instruction.arguments[i];
                    }
                    out << ')';
                    break;
                case Op::Add:
                    out << (instruction.type == Type::Float ? "add.f64 %" : "add.i64 %") << instruction.left
                        << ", %" << instruction.right;
                    break;
                case Op::Subtract:
                    out << (instruction.type == Type::Float ? "sub.f64 %" : "sub.i64 %") << instruction.left
                        << ", %" << instruction.right;
                    break;
                case Op::Multiply:
                    out << (instruction.type == Type::Float ? "mul.f64 %" : "mul.i64 %") << instruction.left
                        << ", %" << instruction.right;
                    break;
                case Op::Divide:
                    out << (instruction.type == Type::Float ? "div.s.f64 %" : "div.s.i64 %")
                        << instruction.left << ", %" << instruction.right;
                    break;
                case Op::Modulo:
                    out << (instruction.type == Type::Float ? "rem.s.f64 %" : "rem.s.i64 %")
                        << instruction.left << ", %" << instruction.right;
                    break;
                case Op::Negate:
                    out << "neg.i64 %" << instruction.left;
                    break;
                case Op::LogicalNot:
                    out << "not.bool %" << instruction.left;
                    break;
                case Op::ToBoolean:
                    out << "to.bool %" << instruction.left;
                    break;
                case Op::Equal:
                    out << (function.valueTypes.at(instruction.left) == Type::Float ? "eq.f64 %" : "eq.i64 %")
                        << instruction.left << ", %" << instruction.right;
                    break;
                case Op::NotEqual:
                    out << (function.valueTypes.at(instruction.left) == Type::Float ? "ne.f64 %" : "ne.i64 %")
                        << instruction.left << ", %" << instruction.right;
                    break;
                case Op::Less:
                    out << (function.valueTypes.at(instruction.left) == Type::Float ? "lt.s.f64 %"
                                                                                    : "lt.s.i64 %")
                        << instruction.left << ", %" << instruction.right;
                    break;
                case Op::LessEqual:
                    out << (function.valueTypes.at(instruction.left) == Type::Float ? "le.s.f64 %"
                                                                                    : "le.s.i64 %")
                        << instruction.left << ", %" << instruction.right;
                    break;
                case Op::Greater:
                    out << (function.valueTypes.at(instruction.left) == Type::Float ? "gt.s.f64 %"
                                                                                    : "gt.s.i64 %")
                        << instruction.left << ", %" << instruction.right;
                    break;
                case Op::GreaterEqual:
                    out << (function.valueTypes.at(instruction.left) == Type::Float ? "ge.s.f64 %"
                                                                                    : "ge.s.i64 %")
                        << instruction.left << ", %" << instruction.right;
                    break;
                case Op::BooleanAnd:
                    out << "and.bool %" << instruction.left << ", %" << instruction.right;
                    break;
                case Op::BooleanOr:
                    out << "or.bool %" << instruction.left << ", %" << instruction.right;
                    break;
                }
                out << '\n';
            }
            switch (block.terminator.kind) {
            case Terminator::Kind::Branch:
                out << "  br " << function.blocks.at(block.terminator.first).name << '\n';
                break;
            case Terminator::Kind::ConditionalBranch:
                out << "  br_if %" << block.terminator.value << ", "
                    << function.blocks.at(block.terminator.first).name << ", "
                    << function.blocks.at(block.terminator.second).name << '\n';
                break;
            case Terminator::Kind::Return:
                out << "  ret %" << block.terminator.value << '\n';
                break;
            default:
                out << "  unreachable\n";
                break;
            }
        }
        out << "}\n";
    }
    return out.str();
}

} // namespace jm::script::ir
