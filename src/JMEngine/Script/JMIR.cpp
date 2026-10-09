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
bool aggregateType(Type type) {
    return type == Type::Vector2 || type == Type::Vector3 || type == Type::Color;
}

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
        if (aggregateType(inferredExpression(value->left, variables, functions)))
            return value->text == "normalized" ? inferredExpression(value->left, variables, functions)
                                               : Type::Float;
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
            auto receiverType = inferredExpression(receiver, variables, functions);
            if (method == "isEmpty" && (receiverType == Type::String || receiverType == Type::List))
                return Type::Bool;
            if (receiverType == Type::Vector2 || receiverType == Type::Vector3)
                return method == "normalized" || method == "cross" || method == "lerp" ? receiverType
                                                                                       : Type::Float;
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
            if (name == "range")
                return Type::Range;
            if (name == "Vector2")
                return Type::Vector2;
            if (name == "Vector3")
                return Type::Vector3;
            if (name == "Color")
                return Type::Color;
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
    std::unordered_map<std::string, const Statement *> structures, globalStructures;
    std::unordered_map<ValueId, const Statement *> valueStructures;
    std::unordered_map<uint32_t, const Statement *> localStructures;
    std::unordered_map<ValueId, std::vector<Type>> tuples;
    std::unordered_map<uint32_t, std::vector<Type>> localTuples;
    std::pair<ValueId, const Statement *> structureReceiver(const ExpressionPtr &expressionValue) {
        auto id = expression(expressionValue);
        if (!valueStructures.contains(id))
            throw std::runtime_error("JM6002: Struct layout is not statically known.");
        return {id, valueStructures.at(id)};
    }
    size_t fieldIndex(const Statement &schema, const std::string &name) {
        for (size_t i = 0; i < schema.body.size(); ++i)
            if (schema.body[i].name == name)
                return i;
        throw std::runtime_error("JM2005: Unknown struct field: " + name);
    }
    ValueId recordGet(ValueId receiver, size_t slot, Type type) {
        return runtime(JM_RT_RECORD_GET, {receiver, integer((static_cast<int64_t>(type) << 32) | slot)},
                       type);
    }

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
        if (type == Type::List || type == Type::Map || type == Type::Optional)
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
        if (actual == Type::Optional) {
            auto payload = runtime(JM_RT_OPTIONAL_GET, {value, integer(static_cast<int>(elementOf(value)))},
                                   elementOf(value));
            if (valueStructures.contains(value))
                valueStructures[payload] = valueStructures.at(value);
            return convert(payload, target);
        }
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
        if (in.type == Type::Struct) {
            if (in.op == Op::Load && localStructures.contains(in.local))
                valueStructures[result] = localStructures.at(in.local);
            else if (in.op == Op::GlobalLoad && globalStructures.contains(in.symbol))
                valueStructures[result] = globalStructures.at(in.symbol);
            else
                throw std::runtime_error("JM6002: Unknown Struct layout.");
        }
        if (in.type == Type::Optional && localStructures.contains(in.local))
            valueStructures[result] = localStructures.at(in.local);
        if (in.type == Type::Tuple && localTuples.contains(in.local))
            tuples[result] = localTuples.at(in.local);
        if ((in.type == Type::List || in.type == Type::Map || in.type == Type::Optional) &&
            localElements.contains(in.local))
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
        in.left =
            in.type == Type::Optional ? optional(value, localElements.at(in.local)) : convert(value, in.type);
        if (in.type == Type::List || in.type == Type::Map || in.type == Type::Optional) {
            auto element = elementOf(in.left);
            if (localElements.contains(in.local) && localElements[in.local] != element)
                throw std::runtime_error("List assignment changes its native element type.");
            localElements[in.local] = element;
            if (in.type == Type::Optional && element == Type::Struct && valueStructures.contains(in.left) &&
                localStructures.contains(in.local) &&
                valueStructures.at(in.left) != localStructures.at(in.local))
                throw std::runtime_error("JM2010: Optional Struct layout mismatch.");
        }
        if (in.type == Type::Struct) {
            if (!valueStructures.contains(value))
                throw std::runtime_error("JM6002: Unknown assigned Struct layout.");
            if (in.op == Op::Store) {
                if (localStructures.contains(in.local) &&
                    localStructures.at(in.local) != valueStructures.at(value))
                    throw std::runtime_error("JM2005: Struct layout assignment mismatch.");
                localStructures[in.local] = valueStructures.at(value);
            } else if (!globalStructures.contains(in.symbol) ||
                       globalStructures.at(in.symbol) != valueStructures.at(value))
                throw std::runtime_error("JM2005: Struct global layout mismatch.");
        }
        if (in.type == Type::Tuple) {
            if (!tuples.contains(value))
                throw std::runtime_error("JM6002: Unknown Tuple layout.");
            if (in.op != Op::Store)
                throw std::runtime_error("JM6002: Tuple globals unsupported.");
            localTuples[in.local] = tuples.at(value);
        }
        at(current).instructions.push_back(in);
    }
    ValueId optional(ValueId value, Type element) {
        auto actual = function.valueTypes.at(value);
        if (actual == Type::Optional) {
            if (elementOf(value) != element)
                throw std::runtime_error("JM2010: Optional payload mismatch.");
            return value;
        }
        if (actual == Type::Null)
            return runtime(JM_RT_OPTIONAL_NONE, {integer(static_cast<int>(element))}, Type::Optional,
                           element);
        auto result =
            runtime(JM_RT_OPTIONAL_SOME, {integer(static_cast<int>(element)), convert(value, element)},
                    Type::Optional, element);
        if (valueStructures.contains(value))
            valueStructures[result] = valueStructures.at(value);
        return result;
    }
    ValueId defaultValue(Type type, Type element = Type::Any) {
        if (type == Type::Optional)
            return runtime(JM_RT_OPTIONAL_NONE, {integer(static_cast<int>(element))}, Type::Optional,
                           element);
        if (type == Type::Map && element == Type::Any)
            throw std::runtime_error("JM6002: Native Map requires explicit value type metadata.");
        if (type == Type::Map)
            return runtime(JM_RT_MAP_CREATE,
                           {integer(static_cast<int>(element == Type::Any ? Type::Int : element))}, Type::Map,
                           element == Type::Any ? Type::Int : element);
        if (aggregateType(type)) {
            auto result = runtime(JM_RT_AGGREGATE_CREATE, {integer(static_cast<int>(type))}, type);
            size_t count = type == Type::Vector2 ? 2 : type == Type::Vector3 ? 3 : 4;
            for (size_t i = 0; i < count; ++i) {
                Instruction field;
                field.op = Op::FloatConstant;
                field.type = Type::Float;
                field.floating = type == Type::Color && i == 3 ? 1 : 0;
                runtime(JM_RT_AGGREGATE_APPEND, {result, emit(field), integer(static_cast<int>(type))},
                        Type::Void);
            }
            return result;
        }

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
    ValueId entityCall(const std::string &property, ValueId receiver, Type result) {
        Instruction in;
        in.op = Op::Call;
        in.symbol = "builtin.entity." + property;
        in.symbolId = stableBuiltinSymbolId(in.symbol);
        in.arguments = {receiver};
        in.type = result;
        return emit(in);
    }
    ValueId chained(ValueId receiver, const std::string &property, bool call = false) {
        if (function.valueTypes[receiver] != Type::Optional)
            throw std::runtime_error("JM2010: ?. requires Optional.");
        auto element = elementOf(receiver);
        Type resultType = element == Type::Struct && valueStructures.contains(receiver)
                              ? valueStructures.at(receiver)
                                    ->body.at(fieldIndex(*valueStructures.at(receiver), property))
                                    .declaredType
                          : element == Type::Entity ? property == "position" ? Type::Vector2
                                                      : property == "id"     ? Type::String
                                                                             : Type::Void
                          : element == Type::String ? Type::Int
                                                    : Type::Float;
        if ((element == Type::Entity && property != "position" && property != "id" &&
             property != "destroy") ||
            (element == Type::String && property != "length") ||
            (aggregateType(element) && property != "x" && property != "y" && property != "z" &&
             property != "length"))
            throw std::runtime_error("JM6002: Unsupported optional chain member.");
        if (resultType == Type::Void && (!call || property != "destroy"))
            throw std::runtime_error("JM6002: Unsupported optional chain call.");
        auto name = "$chain" + std::to_string(nextValue);
        auto slot = declare(name, Type::Optional);
        localElements[slot] = resultType;
        auto some = block("chain.some"), none = block("chain.none"), end = block("chain.end");
        auto has = runtime(JM_RT_OPTIONAL_HAS, {receiver}, Type::Bool);
        at(current).terminator = {Terminator::Kind::ConditionalBranch, has, some, none};
        select(some);
        auto payload = convert(receiver, element);
        ValueId result;
        if (element == Type::Struct && valueStructures.contains(receiver))
            result = recordGet(payload, fieldIndex(*valueStructures.at(receiver), property), resultType);
        else if (element == Type::Entity)
            result = entityCall(property, payload, resultType);
        else if (element == Type::String)
            result =
                runtime(JM_RT_LENGTH, {payload, integer(0), integer(static_cast<int>(element))}, Type::Int);
        else
            result = property == "length" ? runtime(JM_RT_VECTOR_LENGTH, {payload}, Type::Float)
                                          : runtime(JM_RT_FIELD_GET,
                                                    {payload, integer(property == "x"   ? 0
                                                                      : property == "y" ? 1
                                                                                        : 2)},
                                                    Type::Float);
        if (resultType != Type::Void)
            store(name, optional(result, resultType));
        at(current).terminator = {Terminator::Kind::Branch, 0, end, 0};
        select(none);
        if (resultType != Type::Void)
            store(name, defaultValue(Type::Optional, resultType));
        at(current).terminator = {Terminator::Kind::Branch, 0, end, 0};
        select(end);
        return resultType == Type::Void ? integer(0) : load(name);
    }
    ValueId expression(const ExpressionPtr &value, Type expectedElement = Type::Any) {
        if (!value)
            return integer(0);
        switch (value->kind) {
        case Expression::Kind::Literal:
            if (value->literal.isNull()) {
                Instruction in;
                in.op = Op::Constant;
                in.type = Type::Null;
                return emit(in);
            }
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
        case Expression::Kind::Map: {
            if (expectedElement == Type::Any)
                throw std::runtime_error("JM6002: Native Map requires an explicit Map<String,T> annotation.");
            std::vector<std::pair<ValueId, ValueId>> entries;
            Type element = expectedElement;
            for (auto &entry : value->entries) {
                Instruction key;
                key.op = Op::StringConstant;
                key.type = Type::String;
                key.symbol = entry.first;
                auto id = expression(entry.second);
                if (element == Type::Any)
                    element = function.valueTypes[id];
                if (function.valueTypes[id] != element &&
                    !(element == Type::Float && function.valueTypes[id] == Type::Int))
                    throw std::runtime_error("JM6002: Native Map values must be homogeneous.");
                entries.push_back({emit(key), convert(id, element)});
            }
            if (element == Type::Any)
                element = Type::Int;
            if (element != Type::Int && element != Type::Float && element != Type::Bool &&
                element != Type::String)
                throw std::runtime_error("JM6002: Unsupported native Map element type.");
            auto result = runtime(JM_RT_MAP_CREATE, {integer(static_cast<int>(element))}, Type::Map, element);
            for (auto [key, id] : entries)
                runtime(JM_RT_MAP_SET, {result, key, id}, Type::Void);
            return result;
        }
        case Expression::Kind::Tuple: {
            auto result = runtime(JM_RT_RECORD_CREATE, {integer(static_cast<int>(Type::Tuple))}, Type::Tuple);
            for (auto &item : value->elements) {
                auto id = expression(item);
                auto type = function.valueTypes[id];
                if (type == Type::Struct || type == Type::Tuple || type == Type::Any || type == Type::Void)
                    throw std::runtime_error("JM6002: Nested/unknown Tuple field unsupported.");
                runtime(JM_RT_RECORD_APPEND, {result, id, integer(static_cast<int>(type))}, Type::Void);
                tuples[result].push_back(type);
            }
            return result;
        }
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
            auto receiver = expression(value->left);
            auto rawIndex = expression(value->right);
            if (function.valueTypes[receiver] == Type::Map)
                return runtime(JM_RT_MAP_GET, {receiver, convert(rawIndex, Type::String)},
                               elementOf(receiver));
            auto index = convert(rawIndex, Type::Int);
            auto type = function.valueTypes[receiver];
            if (type == Type::Tuple) {
                if (!tuples.contains(receiver) || value->right->kind != Expression::Kind::Literal ||
                    value->right->literal.type() != Type::Int)
                    throw std::runtime_error(
                        "JM6002: Native Tuple index must be a statically known Int literal.");
                auto slot = std::get<int64_t>(value->right->literal.data);
                if (slot < 0 || static_cast<size_t>(slot) >= tuples[receiver].size())
                    throw std::runtime_error("JM3003: Tuple index out of bounds.");
                return recordGet(receiver, slot, tuples[receiver][slot]);
            }
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
            auto operand = expression(value->right);
            if (function.valueTypes[operand] == Type::Optional)
                operand = convert(operand, elementOf(operand));
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
            if ((value->text == "==" || value->text == "!=") &&
                value->left->kind == Expression::Kind::Literal && value->left->literal.isNull() &&
                !(value->right->kind == Expression::Kind::Literal && value->right->literal.isNull())) {
                auto normalized = std::make_shared<Expression>(*value);
                std::swap(normalized->left, normalized->right);
                return expression(normalized);
            }
            if (value->text == "**" || value->text == "^") {
                auto call = std::make_shared<Expression>();
                call->kind = Expression::Kind::Call;
                call->left = std::make_shared<Expression>();
                call->left->kind = Expression::Kind::Identifier;
                call->left->text = "builtin.math.pow";
                call->arguments = {{"", value->left}, {"", value->right}};
                return expression(call);
            }
            auto left = expression(value->left);
            if (value->text == "??") {
                if (function.valueTypes[left] != Type::Optional)
                    throw std::runtime_error("JM2010: ?? requires Optional.");
                auto element = elementOf(left);
                auto name = "$coalesce" + std::to_string(nextValue);
                declare(name, element);
                auto some = block("optional.some"), none = block("optional.none"),
                     end = block("optional.end");
                auto has = runtime(JM_RT_OPTIONAL_HAS, {left}, Type::Bool);
                at(current).terminator = {Terminator::Kind::ConditionalBranch, has, some, none};
                select(some);
                store(name, runtime(JM_RT_OPTIONAL_GET, {left, integer(static_cast<int>(element))}, element));
                at(current).terminator = {Terminator::Kind::Branch, 0, end, 0};
                select(none);
                store(name, convert(expression(value->right), element));
                at(current).terminator = {Terminator::Kind::Branch, 0, end, 0};
                select(end);
                return load(name);
            }
            if ((value->text == "==" || value->text == "!=") &&
                value->right->kind == Expression::Kind::Literal && value->right->literal.isNull()) {
                if (function.valueTypes[left] != Type::Optional) {
                    Instruction result;
                    result.op = Op::Constant;
                    result.type = Type::Bool;
                    result.immediate = (function.valueTypes[left] == Type::Null) == (value->text == "==");
                    return emit(result);
                }
                auto has = runtime(JM_RT_OPTIONAL_HAS, {left}, Type::Bool);
                if (value->text == "!=")
                    return has;
                Instruction in;
                in.op = Op::LogicalNot;
                in.left = has;
                in.type = Type::Bool;
                return emit(in);
            }
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
            auto right = expression(value->right);
            if ((value->text == "==" || value->text == "!=") &&
                (function.valueTypes[left] == Type::Optional ||
                 function.valueTypes[right] == Type::Optional)) {
                auto element =
                    function.valueTypes[left] == Type::Optional ? elementOf(left) : elementOf(right);
                left = optional(left, element);
                right = optional(right, element);
                auto result = runtime(JM_RT_OPTIONAL_EQUAL, {left, right, integer(static_cast<int>(element))},
                                      Type::Bool);
                if (value->text == "==")
                    return result;
                Instruction inverse;
                inverse.op = Op::LogicalNot;
                inverse.left = result;
                inverse.type = Type::Bool;
                return emit(inverse);
            }
            if (function.valueTypes[left] == Type::Optional)
                left = convert(left, elementOf(left));
            if (function.valueTypes[right] == Type::Optional)
                right = convert(right, elementOf(right));
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
            if (aggregateType(leftType) || aggregateType(rightType)) {
                if ((value->text == "==" || value->text == "!=") && leftType == rightType) {
                    auto result = runtime(JM_RT_AGGREGATE_EQUAL, {left, right}, Type::Bool);
                    if (value->text == "==")
                        return result;
                    Instruction inverse;
                    inverse.op = Op::LogicalNot;
                    inverse.left = result;
                    inverse.type = Type::Bool;
                    return emit(inverse);
                }
                if ((value->text == "+" || value->text == "-") && leftType == rightType &&
                    leftType != Type::Color)
                    return runtime(value->text == "+" ? JM_RT_VECTOR_ADD : JM_RT_VECTOR_SUBTRACT,
                                   {left, right}, leftType);
                if ((value->text == "*" || value->text == "/") &&
                    (leftType == Type::Vector2 || leftType == Type::Vector3))
                    return runtime(value->text == "*" ? JM_RT_VECTOR_SCALE : JM_RT_VECTOR_DIVIDE,
                                   {left, convert(right, Type::Float)}, leftType);
                if (value->text == "*" && (rightType == Type::Vector2 || rightType == Type::Vector3))
                    return runtime(JM_RT_VECTOR_SCALE, {right, convert(left, Type::Float)}, rightType);
                throw std::runtime_error("JM6002: Invalid native aggregate operation.");
            }
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
            if (value->text.starts_with('?'))
                return chained(expression(value->left), value->text.substr(1));
            if (value->left && (value->left->kind != Expression::Kind::Identifier ||
                                hasLocal(value->left->text) || global(value->left->text))) {
                auto receiver = expression(value->left);
                auto type = function.valueTypes[receiver];
                if (type == Type::Optional) {
                    receiver = convert(receiver, elementOf(receiver));
                    type = function.valueTypes[receiver];
                }
                if (type == Type::Entity) {
                    auto call = std::make_shared<Expression>();
                    call->kind = Expression::Kind::Call;
                    call->left = std::make_shared<Expression>();
                    call->left->kind = Expression::Kind::Identifier;
                    call->left->text = "builtin.entity." + value->text;
                    call->arguments = {{"", value->left}};
                    return expression(call);
                }
                if (type == Type::Range) {
                    auto slot = value->text == "start"  ? 0
                                : value->text == "end"  ? 1
                                : value->text == "step" ? 2
                                                        : -1;
                    if (slot < 0)
                        throw std::runtime_error("JM2005: Unknown Range field.");
                    return runtime(JM_RT_RANGE_FIELD, {receiver, integer(slot)}, Type::Int);
                }
                if ((type == Type::String || type == Type::List) && value->text == "length")
                    return runtime(JM_RT_LENGTH, {receiver, integer(0), integer(static_cast<int>(type))},
                                   Type::Int);
                if (type == Type::Map && value->text == "length")
                    return runtime(JM_RT_MAP_LENGTH, {receiver}, Type::Int);
                if (type == Type::Struct) {
                    if (!valueStructures.contains(receiver))
                        throw std::runtime_error("JM6002: Unknown Struct field layout.");
                    auto schema = valueStructures[receiver];
                    auto slot = fieldIndex(*schema, value->text);
                    auto &field = schema->body[slot];
                    auto result = recordGet(receiver, slot, field.declaredType);
                    if (field.declaredType == Type::List || field.declaredType == Type::Map ||
                        field.declaredType == Type::Optional)
                        elements[result] = field.elementType;
                    return result;
                }
                if (type == Type::Tuple) {
                    if (!tuples.contains(receiver) || value->text.empty() ||
                        value->text.find_first_not_of("0123456789") != std::string::npos)
                        throw std::runtime_error("JM6002: Tuple member requires a numeric field.");
                    auto slot = std::stoull(value->text);
                    if (slot >= tuples[receiver].size())
                        throw std::runtime_error("JM3003: Tuple field out of bounds.");
                    return recordGet(receiver, slot, tuples[receiver][slot]);
                }
                if (aggregateType(type)) {
                    if (value->text == "length")
                        return runtime(JM_RT_VECTOR_LENGTH, {receiver}, Type::Float);
                    if (value->text == "normalized")
                        return runtime(JM_RT_VECTOR_NORMALIZED, {receiver}, type);
                    if (value->text == "lengthSquared")
                        return runtime(JM_RT_VECTOR_DOT, {receiver, receiver}, Type::Float);
                    std::string fields = type == Type::Color ? "rgba" : type == Type::Vector2 ? "xy" : "xyz";
                    auto field = fields.find(value->text);
                    if (value->text.size() != 1 || field == std::string::npos)
                        throw std::runtime_error("JM2005: Unknown aggregate field: " + value->text);
                    return runtime(JM_RT_FIELD_GET, {receiver, integer(field)}, Type::Float);
                }
            }
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
            if (value->left && value->left->kind == Expression::Kind::Member &&
                value->left->text.starts_with('?')) {
                if (!value->arguments.empty())
                    throw std::runtime_error("JM6002: Optional chain calls with arguments are unsupported.");
                return chained(expression(value->left->left), value->left->text.substr(1), true);
            }
            if (value->left && value->left->kind == Expression::Kind::Member && value->left->left &&
                value->left->left->kind == Expression::Kind::Identifier &&
                hasLocal(value->left->left->text)) {
                auto receiver = load(value->left->left->text);
                auto type = function.valueTypes[receiver];
                if (type == Type::Entity || (type == Type::Optional && elementOf(receiver) == Type::Entity)) {
                    auto call = std::make_shared<Expression>(*value);
                    call->left = std::make_shared<Expression>();
                    call->left->kind = Expression::Kind::Identifier;
                    call->left->text = "builtin.entity." + value->left->text;
                    call->builtinSymbolName.clear();
                    call->builtinSymbolId = 0;
                    call->arguments.insert(call->arguments.begin(), {"", value->left->left});
                    return expression(call);
                }
            }
            if (value->left && value->left->kind == Expression::Kind::Identifier &&
                !declarations.contains(value->left->text)) {
                auto name = value->left->text;
                if (name == "range") {
                    if (value->arguments.size() != 2 && value->arguments.size() != 3)
                        throw std::runtime_error("JM2003: Range arity mismatch.");
                    std::vector<ValueId> args;
                    for (auto &arg : value->arguments) {
                        if (!arg.name.empty())
                            throw std::runtime_error("JM6002: Named Range arguments unsupported.");
                        args.push_back(convert(expression(arg.value), Type::Int));
                    }
                    if (args.size() == 2)
                        args.push_back(integer(1));
                    return runtime(JM_RT_RANGE_CREATE, args, Type::Range);
                }
                if (structures.contains(name)) {
                    auto schema = structures.at(name);
                    std::vector<ValueId> fields(schema->body.size());
                    std::vector<bool> supplied(fields.size());
                    size_t positional = 0;
                    for (auto &arg : value->arguments) {
                        auto slot = arg.name.empty() ? positional++ : fieldIndex(*schema, arg.name);
                        if (slot >= fields.size() || supplied[slot])
                            throw std::runtime_error("JM2003: Struct argument mismatch.");
                        auto &field = schema->body[slot];
                        auto id = expression(arg.value, field.elementType);
                        fields[slot] = field.declaredType == Type::Optional ? optional(id, field.elementType)
                                                                            : convert(id, field.declaredType);
                        supplied[slot] = true;
                    }
                    auto result =
                        runtime(JM_RT_RECORD_CREATE, {integer(static_cast<int>(Type::Struct))}, Type::Struct);
                    for (size_t i = 0; i < fields.size(); ++i) {
                        auto &field = schema->body[i];
                        if (field.declaredType == Type::Any || field.declaredType == Type::Void ||
                            field.declaredType == Type::Struct || field.declaredType == Type::Tuple)
                            throw std::runtime_error(
                                "JM6002: Native Struct requires concrete non-record fields.");
                        if (!supplied[i]) {
                            if (!field.expression)
                                throw std::runtime_error("JM2003: Missing Struct field: " + field.name);
                            fields[i] = field.declaredType == Type::Optional
                                            ? optional(expression(field.expression, field.elementType),
                                                       field.elementType)
                                            : convert(expression(field.expression, field.elementType),
                                                      field.declaredType);
                        }
                        runtime(JM_RT_RECORD_APPEND,
                                {result, fields[i], integer(static_cast<int>(field.declaredType))},
                                Type::Void);
                    }
                    valueStructures[result] = schema;
                    return result;
                }
                Type type = name == "Vector2"   ? Type::Vector2
                            : name == "Vector3" ? Type::Vector3
                            : name == "Color"   ? Type::Color
                                                : Type::Any;
                if (aggregateType(type)) {
                    size_t count = type == Type::Vector2 ? 2 : type == Type::Vector3 ? 3 : 4;
                    if (value->arguments.size() != count &&
                        !(type == Type::Color && value->arguments.size() == 3))
                        throw std::runtime_error("JM2007: Aggregate constructor arity mismatch.");
                    auto result = runtime(JM_RT_AGGREGATE_CREATE, {integer(static_cast<int>(type))}, type);
                    for (auto &arg : value->arguments) {
                        if (!arg.name.empty())
                            throw std::runtime_error("Named vector constructor arguments unsupported.");
                        runtime(JM_RT_AGGREGATE_APPEND,
                                {result, convert(expression(arg.value), Type::Float),
                                 integer(static_cast<int>(type))},
                                Type::Void);
                    }
                    if (value->arguments.size() < count) {
                        Instruction one;
                        one.op = Op::FloatConstant;
                        one.type = Type::Float;
                        one.floating = 1;
                        runtime(JM_RT_AGGREGATE_APPEND, {result, emit(one), integer(static_cast<int>(type))},
                                Type::Void);
                    }
                    return result;
                }
            }
            if (value->left && value->left->kind == Expression::Kind::Member) {
                const auto &member = *value->left;
                if (member.left && (member.left->kind != Expression::Kind::Identifier ||
                                    hasLocal(member.left->text) || global(member.left->text))) {
                    auto receiver = expression(member.left);
                    auto type = function.valueTypes[receiver];
                    if (type == Type::Optional) {
                        receiver = convert(receiver, elementOf(receiver));
                        type = function.valueTypes[receiver];
                    }
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
                    if ((type == Type::String || type == Type::List) && member.text == "isEmpty") {
                        arity(0);
                        auto length = runtime(JM_RT_LENGTH,
                                              {receiver, integer(0), integer(static_cast<int>(type))}, Type::Int);
                        Instruction empty;
                        empty.op = Op::Equal;
                        empty.left = length;
                        empty.right = integer(0);
                        empty.type = Type::Bool;
                        return emit(empty);
                    }
                    if (type == Type::Map) {
                        auto method = member.text;
                        if (method == "containsKey" || method == "remove") {
                            arity(1);
                            return runtime(method == "containsKey" ? JM_RT_MAP_CONTAINS : JM_RT_MAP_REMOVE,
                                           {receiver, convert(args[0], Type::String)}, Type::Bool);
                        }
                        arity(0);
                        if (method == "clear")
                            return runtime(JM_RT_MAP_CLEAR, {receiver}, Type::Void);
                        if (method == "keys" || method == "values")
                            return runtime(method == "keys" ? JM_RT_MAP_KEYS : JM_RT_MAP_VALUES, {receiver},
                                           Type::List, method == "keys" ? Type::String : elementOf(receiver));
                        throw std::runtime_error("JM6002: Unknown native Map method.");
                    }
                    if (type == Type::Vector2 || type == Type::Vector3) {
                        static const std::unordered_map<std::string, int> ops{
                            {"length", JM_RT_VECTOR_LENGTH}, {"normalized", JM_RT_VECTOR_NORMALIZED},
                            {"dot", JM_RT_VECTOR_DOT},       {"distance", JM_RT_VECTOR_DISTANCE},
                            {"cross", JM_RT_VECTOR_CROSS},   {"lerp", JM_RT_VECTOR_LERP}};
                        auto found = ops.find(member.text);
                        if (found == ops.end())
                            throw std::runtime_error("Unsupported native vector method.");
                        std::vector<ValueId> vectorArgs{receiver};
                        for (size_t i = 0; i < args.size(); ++i)
                            vectorArgs.push_back(convert(args[i], i == 1 ? Type::Float : type));
                        return runtime(found->second, vectorArgs,
                                       member.text == "length" || member.text == "dot" ||
                                               member.text == "distance"
                                           ? Type::Float
                                           : type);
                    }
                    if (type == Type::String) {
                        const std::unordered_map<std::string, std::pair<int, Type>> methods{
                            {"substring", {JM_RT_SUBSTRING, Type::String}},
                            {"slice", {JM_RT_SUBSTRING, Type::String}},
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
                if (name == "bool") {
                    if (value->arguments.size() != 1)
                        throw std::runtime_error("bool requires one argument.");
                    auto id = expression(value->arguments[0].value);
                    auto actual = function.valueTypes[id];
                    if (actual == Type::Bool)
                        return id;
                    if (actual == Type::String)
                        id = runtime(JM_RT_LENGTH, {id, integer(0), integer(static_cast<int>(Type::String))},
                                     Type::Int);
                    else if (actual != Type::Int && actual != Type::Float)
                        throw std::runtime_error(
                            "Native bool conversion accepts Bool, Int, Float, or String.");
                    Instruction conversion;
                    conversion.op = Op::ToBoolean;
                    conversion.left = id;
                    conversion.type = Type::Bool;
                    return emit(conversion);
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
                    result = type == Type::Optional
                                 ? optional(result, found->second->parameterElementTypes.at(index))
                                 : convert(result, type == Type::Any ? Type::Int : type);
                    if ((type == Type::List || type == Type::Map || type == Type::Optional) &&
                        index < found->second->parameterElementTypes.size() &&
                        found->second->parameterElementTypes[index] != Type::Any &&
                        elementOf(result) != found->second->parameterElementTypes[index])
                        throw std::runtime_error("Typed List parameter element mismatch.");
                }
                if (metadata) {
                    if (!argument.name.empty() && argument.name != metadata->parameterNames[index])
                        throw std::runtime_error("Typed FFI named arguments must match metadata order.");
                    result = convert(result, metadata->parameterTypes[index]);
                    if (metadata->parameterTypes[index] == Type::List &&
                        elementOf(result) != metadata->parameterElementTypes[index])
                        throw std::runtime_error("JM7001: Typed FFI List element mismatch.");
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
            if (metadata && (metadata->returnType == Type::List || metadata->returnType == Type::Optional))
                elements[id] = metadata->returnElementType;
            if (found != declarations.end() &&
                (found->second->returnType == Type::List || found->second->returnType == Type::Map ||
                 found->second->returnType == Type::Optional))
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
                    type != Type::Float && type != Type::Struct && type != Type::Tuple && type != Type::Map &&
                    type != Type::Range && type != Type::Optional && type != Type::Entity &&
                    !aggregateType(type))
                    throw std::runtime_error("Native local '" + statement.name + "' has unsupported type " +
                                             typeName(type) + "; use Interpreter.");
                auto slot = declare(statement.name, type, statement.constant);
                if (type == Type::Optional)
                    localElements[slot] =
                        statement.elementType == Type::Any ? elementOf(value) : statement.elementType;
                if (type == Type::Optional && statement.elementType == Type::Struct) {
                    if (!structures.contains(statement.declaredTypeName))
                        throw std::runtime_error("JM2010: Optional Struct schema missing.");
                    localStructures[slot] = structures.at(statement.declaredTypeName);
                }
                store(statement.name, value, true);
                break;
            }
            case Statement::Kind::Assignment: {
                if (statement.target && statement.target->kind == Expression::Kind::Member &&
                    statement.target->left && statement.target->left->kind == Expression::Kind::Identifier &&
                    hasLocal(statement.target->left->text)) {
                    auto receiver = load(statement.target->left->text);
                    auto type = function.valueTypes[receiver];
                    if (type == Type::Entity ||
                        (type == Type::Optional && elementOf(receiver) == Type::Entity)) {
                        auto argument = statement.expression;
                        if (statement.operation != "=") {
                            argument = std::make_shared<Expression>();
                            argument->kind = Expression::Kind::Binary;
                            argument->text = statement.operation.substr(0, 1);
                            argument->left = statement.target;
                            argument->right = statement.expression;
                        }
                        auto property = statement.target->text;
                        property[0] =
                            static_cast<char>(std::toupper(static_cast<unsigned char>(property[0])));
                        auto call = std::make_shared<Expression>();
                        call->kind = Expression::Kind::Call;
                        call->left = std::make_shared<Expression>();
                        call->left->kind = Expression::Kind::Identifier;
                        call->left->text = "builtin.entity.set" + property;
                        call->arguments = {{"", statement.target->left}, {"", argument}};
                        expression(call);
                        break;
                    }
                }
                if (statement.target && statement.target->kind == Expression::Kind::Member &&
                    statement.target->left && statement.target->left->kind == Expression::Kind::Identifier &&
                    !hasLocal(statement.target->left->text) && !global(statement.target->left->text)) {
                    auto property = statement.target->text;
                    if (property != "position" && property != "velocity" && property != "scale" &&
                        property != "rotation")
                        throw std::runtime_error("JM6002: Unsupported writable host property.");
                    auto argument = statement.expression;
                    if (statement.operation != "=") {
                        if (statement.operation != "+=" && statement.operation != "-=")
                            throw std::runtime_error("Unsupported host compound assignment.");
                        argument = std::make_shared<Expression>();
                        argument->kind = Expression::Kind::Binary;
                        argument->text = statement.operation.substr(0, 1);
                        argument->left = statement.target;
                        argument->right = statement.expression;
                    }
                    property[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(property[0])));
                    auto member = std::make_shared<Expression>();
                    member->kind = Expression::Kind::Member;
                    member->left = statement.target->left;
                    member->text = "set" + property;
                    auto call = std::make_shared<Expression>();
                    call->kind = Expression::Kind::Call;
                    call->left = member;
                    call->builtinSymbolName = "builtin." + member->left->text + "." + member->text;
                    call->builtinSymbolId = stableBuiltinSymbolId(call->builtinSymbolName);
                    call->arguments = {{"", argument}};
                    expression(call);
                    break;
                }
                if (statement.target && statement.target->kind == Expression::Kind::Member) {
                    auto [receiver, schema] = structureReceiver(statement.target->left);
                    auto slot = fieldIndex(*schema, statement.target->text);
                    auto &field = schema->body[slot];
                    auto result =
                        field.declaredType == Type::Optional
                            ? optional(expression(statement.expression, field.elementType), field.elementType)
                            : convert(expression(statement.expression, field.elementType),
                                      field.declaredType);
                    if (statement.operation != "=") {
                        auto old = recordGet(receiver, slot, field.declaredType);
                        if (field.declaredType == Type::Vector2 || field.declaredType == Type::Vector3) {
                            if (statement.operation != "+=" && statement.operation != "-=")
                                throw std::runtime_error(
                                    "JM6002: Unsupported Struct Vector compound operation.");
                            result = runtime(statement.operation == "+=" ? JM_RT_VECTOR_ADD
                                                                         : JM_RT_VECTOR_SUBTRACT,
                                             {old, result}, field.declaredType);
                        } else {
                            Instruction op;
                            op.left = old;
                            op.right = result;
                            op.type = field.declaredType;
                            op.op = statement.operation == "+="   ? Op::Add
                                    : statement.operation == "-=" ? Op::Subtract
                                    : statement.operation == "*=" ? Op::Multiply
                                    : statement.operation == "/=" ? Op::Divide
                                                                  : Op::Modulo;
                            if (field.declaredType != Type::Int && field.declaredType != Type::Float)
                                throw std::runtime_error("JM6002: Unsupported Struct compound operation.");
                            result = emit(op);
                        }
                    }
                    runtime(
                        JM_RT_RECORD_SET,
                        {receiver, integer((static_cast<int64_t>(field.declaredType) << 32) | slot), result},
                        Type::Void);
                    break;
                }
                if (statement.target && statement.target->kind == Expression::Kind::Index) {
                    if (statement.operation != "=")
                        throw std::runtime_error("Native compound indexed assignment is not supported yet.");
                    auto receiver = expression(statement.target->left);
                    if (function.valueTypes[receiver] == Type::Map) {
                        runtime(JM_RT_MAP_SET,
                                {receiver, convert(expression(statement.target->right), Type::String),
                                 convert(expression(statement.expression), elementOf(receiver))},
                                Type::Void);
                        break;
                    }
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
                    if (type == Type::Vector2 || type == Type::Vector3) {
                        if (statement.operation != "+=" && statement.operation != "-=")
                            throw std::runtime_error("Native vector compound operation requires +=/-=.");
                        result =
                            runtime(statement.operation == "+=" ? JM_RT_VECTOR_ADD : JM_RT_VECTOR_SUBTRACT,
                                    {old, in.right}, type);
                        store(statement.target->text, result);
                        break;
                    }
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
                if (function.returnType == Type::List || function.returnType == Type::Map) {
                    auto element = elementOf(result);
                    if (function.returnElementType != Type::Any && function.returnElementType != element)
                        throw std::runtime_error("Typed List return element mismatch.");
                    function.returnElementType = element;
                }
                const auto converted = function.returnType == Type::Void ? result
                                       : function.returnType == Type::Optional
                                           ? optional(result, function.returnElementType)
                                           : convert(result, function.returnType);
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
                auto isRange = function.valueTypes[source] == Type::Range;
                if (function.valueTypes[source] != Type::List && !isRange)
                    throw std::runtime_error("Native foreach requires homogeneous List or Range.");
                auto element = isRange ? Type::Int : elementOf(source);
                auto snapshot = isRange ? source : runtime(JM_RT_LIST_CLONE, {source}, Type::List, element);
                auto end = isRange ? runtime(JM_RT_RANGE_LENGTH, {snapshot}, Type::Int)
                                   : runtime(JM_RT_LENGTH,
                                             {snapshot, integer(0), integer(static_cast<int>(Type::List))},
                                             Type::Int);
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
                      isRange
                          ? runtime(JM_RT_RANGE_GET, {snapshot, load(counter)}, Type::Int)
                          : runtime(JM_RT_LIST_GET,
                                    {snapshot, load(counter), integer(static_cast<int>(element))}, element),
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
            if (type == Type::List || type == Type::Map || type == Type::Optional) {
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
        function.valueElementTypes = elements;
        return std::move(function);
    }
};

} // namespace

bool lower(const Program &program, Module &output, LoweringDiagnostic &diagnostic,
           const NativeFunctionRegistry *registry) {
    try {
        std::vector<Diagnostic> errors;
        if (!check(program, errors, registry))
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
        std::unordered_map<std::string, const Statement *> structures, globalStructures;
        for (auto &item : normalized)
            if (item.kind == Statement::Kind::Struct)
                structures[item.name] = &item;
        std::unordered_map<std::string, Type> variables, signatures;
        for (auto &[name, schema] : structures)
            signatures[name] = Type::Struct;
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
                if (type != Type::String && type != Type::Int && type != Type::Float && type != Type::Bool &&
                    type != Type::Struct && !aggregateType(type))
                    throw std::runtime_error("Global " + item.name +
                                             " uses a non-scalar type; use Interpreter.");
                if (type == Type::Struct) {
                    if (!item.expression || item.expression->kind != Expression::Kind::Call ||
                        !item.expression->left || !structures.contains(item.expression->left->text))
                        throw std::runtime_error("JM6002: Global Struct requires a direct constructor.");
                    globalStructures[item.name] = structures.at(item.expression->left->text);
                }
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
            } else if (item.kind == Statement::Kind::Struct) {
                continue;
            } else if (item.kind == Statement::Kind::Enum)
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
                lowerer.structures = structures;
                lowerer.globalStructures = globalStructures;
                result.functions.push_back(lowerer.build(item));
            }
        if (!result.globals.empty()) {
            Lowerer lowerer;
            lowerer.module = &result;
            lowerer.registry = registry;
            lowerer.declarations = declarations;
            lowerer.structures = structures;
            lowerer.globalStructures = globalStructures;
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
