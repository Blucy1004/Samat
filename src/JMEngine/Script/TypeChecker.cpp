#include "JMEngine/Script/LanguageCore.hpp"
#include "JMEngine/Script/StandardLibrary.hpp"
#include <algorithm>
#include <unordered_set>

namespace jm::script {
std::string typeName(Type type) {
    static const char *names[]{"Any",     "Int",     "Float", "Bool", "String", "Void",  "List", "Map",
                               "Vector2", "Vector3", "Color", "Enum", "Struct", "Tuple", "Range"};
    return names[static_cast<unsigned>(type)];
}
std::string annotationName(Type base, Type element) {
    return typeName(base) +
           (element == Type::Any ? "" : (base == Type::Map ? "<String, " : "<") + typeName(element) + ">");
}
TypeAnnotation parseAnnotation(std::string_view name) {
    auto less = name.find('<');
    if (less == std::string_view::npos)
        return {parseType(name), Type::Any};
    if (name.substr(0, less) == "Map") {
        auto comma = name.find(',', less);
        if (!name.ends_with('>') || comma == std::string_view::npos ||
            name.substr(less + 1, comma - less - 1) != "String")
            throw std::runtime_error("Map<K,V> currently requires String keys.");
        auto text = name.substr(comma + 1, name.size() - comma - 2);
        while (!text.empty() && text.front() == ' ')
            text.remove_prefix(1);
        auto element = parseType(text);
        if (element != Type::Int && element != Type::Float && element != Type::Bool &&
            element != Type::String)
            throw std::runtime_error("Map<String,T> currently requires primitive values.");
        return {Type::Map, element};
    }
    if (!name.ends_with('>') || name.find(',', less) != std::string_view::npos)
        throw std::runtime_error("Only List<T> annotations are currently supported.");
    auto base = parseType(name.substr(0, less));
    auto element = parseType(name.substr(less + 1, name.size() - less - 2));
    if (base != Type::List || element == Type::Void || element == Type::Any || element == Type::List ||
        element == Type::Map)
        throw std::runtime_error("List<T> requires a concrete non-collection element type.");
    return {base, element};
}
Type parseType(std::string_view name) {
    if (name == "Int" || name == "i64" || name == "정수")
        return Type::Int;
    if (name == "Float" || name == "f64" || name == "실수")
        return Type::Float;
    if (name == "Bool" || name == "Boolean" || name == "논리")
        return Type::Bool;
    if (name == "String" || name == "문자열")
        return Type::String;
    if (name == "Void")
        return Type::Void;
    if (name == "List" || name == "목록")
        return Type::List;
    if (name == "Map" || name == "지도")
        return Type::Map;
    if (name == "Tuple")
        return Type::Tuple;
    if (name == "Range")
        return Type::Range;
    if (name == "Vector3")
        return Type::Vector3;
    if (name == "Color")
        return Type::Color;
    if (name == "Vector2")
        return Type::Vector2;
    if (name == "Any" || name == "숫자" || name == "자동")
        return Type::Any;
    throw std::runtime_error("Unknown type '" + std::string(name) +
                             "'. Use Int, Float, Bool, String, List, Map, Vector2, or Void.");
}
Type Value::type() const {
    switch (data.index()) {
    case 0:
        return Type::Void;
    case 1:
        return Type::Bool;
    case 2:
        return Type::Float;
    case 3:
        return Type::String;
    case 4:
        return Type::List;
    case 5:
        return Type::Map;
    case 6:
        return Type::Vector2;
    case 7:
        return Type::Color;
    case 10:
        return Type::Vector3;
    case 11:
        return Type::Enum;
    case 12:
        return Type::Struct;
    case 13:
        return Type::Tuple;
    case 14:
        return Type::Range;
    case 9:
        return Type::Int;
    default:
        return Type::Any;
    }
}
namespace {
bool numeric(Type type) { return type == Type::Int || type == Type::Float || type == Type::Any; }
bool accepts(Type target, Type source) {
    return target == Type::Any || source == Type::Any || target == source ||
           (target == Type::Float && source == Type::Int);
}
struct Checker {
    struct Binding {
        Type type;
        bool constant;
        const Statement *structure{};
        Type element{Type::Any};
    };
    std::vector<std::unordered_map<std::string, Binding>> scopes{1};
    std::unordered_map<std::string, const Statement *> functions, structures;
    std::vector<Diagnostic> &errors;
    explicit Checker(std::vector<Diagnostic> &diagnostics) : errors(diagnostics) {}
    std::size_t line{};
    void error(std::string code, std::string message, Type expected = Type::Any, Type actual = Type::Any) {
        Diagnostic diagnostic{std::move(message), line};
        diagnostic.code = std::move(code);
        diagnostic.expected = typeName(expected);
        diagnostic.actual = typeName(actual);
        diagnostic.suggestion = "Check the declaration and operand types.";
        errors.push_back(std::move(diagnostic));
    }
    Binding lookup(const std::string &name) const {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
            if (auto it = scope->find(name); it != scope->end())
                return it->second;
        return {Type::Any, false}; // Host values can be injected at execution time.
    }
    void declare(const std::string &name, Binding binding) {
        if (!scopes.back().emplace(name, binding).second)
            error("JM2004", "Duplicate declaration: " + name);
    }
    Type expr(const ExpressionPtr &value) {
        if (!value)
            return Type::Void;
        switch (value->kind) {
        case Expression::Kind::Literal:
            return value->literal.type();
        case Expression::Kind::Identifier:
            return lookup(value->text).type;
        case Expression::Kind::Tuple:
            for (const auto &item : value->elements)
                expr(item);
            return Type::Tuple;
        case Expression::Kind::Array:
            for (const auto &item : value->elements)
                expr(item);
            return Type::List;
        case Expression::Kind::Map:
            for (const auto &item : value->entries)
                expr(item.second);
            return Type::Map;
        case Expression::Kind::Index: {
            auto container = expr(value->left), index = expr(value->right);
            if ((container == Type::List || container == Type::String) && index != Type::Int &&
                index != Type::Any)
                error("JM2005", "List/string index must be Int.", Type::Int, index);
            if (value->left && value->left->kind == Expression::Kind::Identifier) {
                auto binding = lookup(value->left->text);
                if (container == Type::Map && binding.element != Type::Any && index != Type::String &&
                    index != Type::Any)
                    error("JM2005", "Typed Map key must be String.", Type::String, index);
                if (container == Type::List || container == Type::Map)
                    return binding.element;
            }
            return container == Type::String ? Type::String : Type::Any;
        }
        case Expression::Kind::Member:
            if (value->left && value->left->kind == Expression::Kind::Identifier &&
                lookup(value->left->text).structure != nullptr) {
                auto schema = lookup(value->left->text).structure;
                auto field = std::find_if(schema->body.begin(), schema->body.end(),
                                          [&](const auto &f) { return f.name == value->text; });
                if (field == schema->body.end()) {
                    error("JM2005", "Unknown Struct field: " + value->text);
                    return Type::Any;
                }
                return field->declaredType;
            }
            if (auto type = expr(value->left);
                type == Type::Vector2 || type == Type::Vector3 || type == Type::Color)
                return value->text == "normalized" ? type : Type::Float;
            return value->text == "length" ? Type::Int : Type::Any;
        case Expression::Kind::Unary: {
            auto operand = expr(value->right);
            if (value->text == "!" || value->text == "not")
                return Type::Bool;
            if (value->text == "~" && operand != Type::Int && operand != Type::Any)
                error("JM2006", "Bitwise complement requires Int.");
            if (!numeric(operand))
                error("JM2006", "Unary arithmetic requires a numeric operand.", Type::Int, operand);
            return operand;
        }
        case Expression::Kind::Binary: {
            auto left = expr(value->left), right = expr(value->right);
            const auto &op = value->text;
            if ((op == "&" || op == "|" || op == "<<" || op == ">>") &&
                ((left != Type::Int && left != Type::Any) || (right != Type::Int && right != Type::Any)))
                error("JM2006", "Bitwise operators require Int operands.");
            if (op == "and" || op == "or" || op == "&&" || op == "||" || op == "==" || op == "!=")
                return Type::Bool;
            if (op == "*" && numeric(left) && (right == Type::Vector2 || right == Type::Vector3))
                return right;
            if (left == Type::Vector2 || left == Type::Vector3) {
                if (((op == "+" || op == "-") && left == right) ||
                    ((op == "*" || op == "/") && numeric(right)))
                    return left;
                error("JM2006", "Vector arithmetic requires matching vectors or a numeric scale.");
                return left;
            }
            if (op == "+" && left == Type::String) {
                if (right != Type::String && right != Type::Any)
                    error("JM2006", "String concatenation requires two Strings.", Type::String, right);
                return Type::String;
            }
            if (op == "+" && left == Type::List)
                return Type::List;
            if (!(numeric(left) && numeric(right)) && !(left == Type::String && right == Type::String))
                error("JM2006",
                      "Operator '" + op + "' cannot combine " + typeName(left) + " and " + typeName(right));
            if (op == "<" || op == "<=" || op == ">" || op == ">=")
                return Type::Bool;
            return left == Type::Float || right == Type::Float || op == "^" || op == "**" ? Type::Float
                   : left == Type::Any || right == Type::Any                              ? Type::Any
                                                                                          : Type::Int;
        }
        case Expression::Kind::Call: {
            std::vector<Type> arguments;
            for (const auto &item : value->arguments)
                arguments.push_back(expr(item.value));
            if (value->left && value->left->kind == Expression::Kind::Member) {
                auto receiver = expr(value->left->left);
                auto method = value->left->text;
                if (receiver == Type::Vector2 || receiver == Type::Vector3) {
                    if (method == "normalized" || method == "cross" || method == "lerp")
                        return receiver;
                    return Type::Float;
                }
                if (receiver == Type::String) {
                    if (method == "contains" || method == "startsWith" || method == "endsWith")
                        return Type::Bool;
                    if (method == "find" || method == "codepointLength")
                        return Type::Int;
                    if (method == "split")
                        return Type::List;
                    return Type::String;
                }
                if (receiver == Type::List || receiver == Type::Map) {
                    if (method == "contains" || method == "containsKey" || method == "remove")
                        return Type::Bool;
                    if (method == "indexOf")
                        return Type::Int;
                    if (method == "keys" || method == "values")
                        return Type::List;
                    if (method == "clear" || method == "push" || method == "append" || method == "insert" ||
                        method == "reverse" || method == "sort")
                        return Type::Void;
                }
            }
            if (!value->left || value->left->kind != Expression::Kind::Identifier) {
                expr(value->left);
                return Type::Any;
            }
            const auto &name = value->left->text;
            if (auto found = functions.find(name); found != functions.end()) {
                const auto &fn = *found->second;
                if (arguments.size() != fn.parameters.size())
                    error("JM2003", "Function '" + name + "' requires " +
                                        std::to_string(fn.parameters.size()) + " arguments, got " +
                                        std::to_string(arguments.size()) + ".");
                for (std::size_t i = 0; i < std::min(arguments.size(), fn.parameters.size()); ++i) {
                    Type target = i < fn.parameterTypes.size() ? fn.parameterTypes[i] : Type::Any;
                    if (!accepts(target, arguments[i]))
                        error("JM2003",
                              "Parameter '" + fn.parameters[i] + "' requires " + typeName(target) + ".",
                              target, arguments[i]);
                    if (!value->arguments[i].name.empty() && value->arguments[i].name != fn.parameters[i])
                        error("JM2003", "Named argument order must match the function declaration.");
                }
                return fn.returnType;
            }
            if (auto structure = structures.find(name); structure != structures.end()) {
                const auto &fields = structure->second->body;
                std::unordered_set<size_t> assigned;
                for (size_t i = 0; i < arguments.size(); ++i) {
                    size_t field = i;
                    if (!value->arguments[i].name.empty()) {
                        field = fields.size();
                        for (size_t j = 0; j < fields.size(); ++j)
                            if (fields[j].name == value->arguments[i].name)
                                field = j;
                    }
                    if (field >= fields.size() || !assigned.insert(field).second)
                        error("JM2003", "Unknown or duplicate struct argument.");
                    else if (!accepts(fields[field].declaredType, arguments[i]))
                        error("JM2003", "Struct field type mismatch.", fields[field].declaredType,
                              arguments[i]);
                }
                for (size_t i = 0; i < fields.size(); ++i)
                    if (!assigned.contains(i) && !fields[i].expression)
                        error("JM2003", "Missing struct field: " + fields[i].name);
                return Type::Struct;
            }
            if (auto standard = standardFunction(name)) {
                if (arguments.size() < standard->minimumArity || arguments.size() > standard->maximumArity)
                    error("JM2003", "Standard library argument count mismatch.");
                for (auto type : arguments)
                    if (standard->numericArguments && !numeric(type))
                        error("JM2003", "Standard numeric function requires numeric arguments.");
                if (standard->returnType != Type::Any)
                    return standard->returnType;
            }
            if (name == "int" || name == "float" || name == "string") {
                if (arguments.size() != 1)
                    error("JM2003", "Conversion requires one argument.");
                return name == "int" ? Type::Int : name == "float" ? Type::Float : Type::String;
            }
            if (name == "bitXor")
                return Type::Int;
            if (name == "print" || name == "println")
                return Type::Void;
            if (name == "len" || name == "length")
                return Type::Int;
            if (name == "assert")
                return Type::Bool;
            if (name == "range")
                return Type::Range;
            if (name == "Vector3" || name == "vector3")
                return Type::Vector3;
            if (name == "Color" || name == "color")
                return Type::Color;
            if (name == "Vector2" || name == "vector2")
                return Type::Vector2;
            if (name == "abs")
                return arguments.empty() ? Type::Any : arguments[0];
            if (name == "min" || name == "max") {
                if (arguments.empty())
                    return Type::Any;
                for (auto type : arguments)
                    if (type == Type::Float)
                        return Type::Float;
                return arguments[0];
            }
            if (name == "sqrt" || name == "pow" || name == "round" || name == "floor" || name == "ceil" ||
                name == "sin" || name == "cos" || name == "tan" || name == "clamp")
                return Type::Float;
            return Type::Any; // Open host-function registry boundary.
        }
        }
        return Type::Any;
    }
    bool returns(const StatementList &list) const {
        for (const auto &item : list) {
            if (item.kind == Statement::Kind::Return)
                return true;
            if (item.kind == Statement::Kind::If && returns(item.body) && returns(item.alternative))
                return true;
        }
        return false;
    }
    void block(const StatementList &list, Type returnType = Type::Any, int loops = 0, bool function = false) {
        for (const auto &item : list) {
            line = item.line;
            switch (item.kind) {
            case Statement::Kind::Enum:
            case Statement::Kind::Struct: {
                if (function || scopes.size() > 1)
                    error("JM2009", "Data declarations require module scope.");
                declare(item.name, {item.kind == Statement::Kind::Enum ? Type::Map : Type::Struct, true});
                std::unordered_set<std::string> fields;
                for (const auto &field : item.body) {
                    if (!fields.insert(field.name).second)
                        error("JM2004", "Duplicate data field: " + field.name);
                    if (item.kind == Statement::Kind::Struct && field.expression &&
                        !accepts(field.declaredType, expr(field.expression)))
                        error("JM2001", "Struct default field type mismatch.");
                }
                break;
            }
            case Statement::Kind::Variable: {
                const Statement *structure = nullptr;
                if (item.expression && item.expression->kind == Expression::Kind::Call &&
                    item.expression->left && structures.contains(item.expression->left->text))
                    structure = structures.at(item.expression->left->text);
                if (item.expression && item.expression->kind == Expression::Kind::Identifier)
                    structure = lookup(item.expression->text).structure;
                auto actual = item.expression ? expr(item.expression) : Type::Any;
                if (actual == Type::Void && item.declaredType == Type::Any)
                    actual = Type::Any;
                if (item.declaredType == Type::Void)
                    error("JM2001", "Variables cannot have type Void.");
                if (item.expression && !accepts(item.declaredType, actual))
                    error("JM2001",
                          "Variable '" + item.name + "' requires " + typeName(item.declaredType) + ", got " +
                              typeName(actual) + ".",
                          item.declaredType, actual);
                if (item.elementType != Type::Any && item.expression &&
                    item.expression->kind == Expression::Kind::Array)
                    for (const auto &value : item.expression->elements)
                        if (!accepts(item.elementType, expr(value)))
                            error("JM2001", "Typed List element mismatch.", item.elementType, expr(value));
                declare(item.name, {item.declaredType == Type::Any ? actual : item.declaredType,
                                    item.constant, structure});
                break;
            }
            case Statement::Kind::Assignment: {
                if (!item.target || (item.target->kind != Expression::Kind::Identifier &&
                                     item.target->kind != Expression::Kind::Index &&
                                     item.target->kind != Expression::Kind::Member))
                    error("JM2001", "Assignment requires a variable or collection member.");
                auto target = expr(item.target), actual = expr(item.expression);
                if (item.target && item.target->kind == Expression::Kind::Identifier &&
                    lookup(item.target->text).constant)
                    error("JM2007", "Cannot modify constant '" + item.target->text + "'.");
                if (!accepts(target, actual))
                    error("JM2001", "Assignment type mismatch.", target, actual);
                break;
            }
            case Statement::Kind::Expression:
                expr(item.expression);
                break;
            case Statement::Kind::Return:
                if (!function)
                    error("JM2009", "return requires a function.");
                if (auto actual = expr(item.expression); !accepts(returnType, actual))
                    error("JM2002", "Return type mismatch.", returnType, actual);
                break;
            case Statement::Kind::Break:
            case Statement::Kind::Continue:
                if (!loops)
                    error("JM2009", "break/continue requires an enclosing loop.");
                break;
            case Statement::Kind::Event: {
                if (function || scopes.size() > 1)
                    error("JM2009", "Event handlers must be declared at module scope.");
                scopes.emplace_back();
                block(item.body, Type::Void, 0, true);
                scopes.pop_back();
                break;
            }
            case Statement::Kind::Function: {
                if (function || scopes.size() > 1)
                    error("JM2009", "Nested function declarations are not supported.");
                scopes.emplace_back();
                for (std::size_t i = 0; i < item.parameters.size(); ++i) {
                    if (i < item.parameterTypes.size() && item.parameterTypes[i] == Type::Void)
                        error("JM2003", "Function parameters cannot have type Void.");
                    declare(item.parameters[i],
                            {i < item.parameterTypes.size() ? item.parameterTypes[i] : Type::Any, false});
                }
                block(item.body, item.returnType, 0, true);
                scopes.pop_back();
                if (item.returnType != Type::Any && item.returnType != Type::Void && !returns(item.body))
                    error("JM2002", "Function '" + item.name + "' can finish without returning " +
                                        typeName(item.returnType) + ".");
                break;
            }
            case Statement::Kind::If:
            case Statement::Kind::While:
            case Statement::Kind::ForRange:
            case Statement::Kind::ForEach: {
                auto start = expr(item.expression);
                if (item.kind == Statement::Kind::ForRange) {
                    auto end = expr(item.rangeEnd);
                    if (!accepts(Type::Int, start) || !accepts(Type::Int, end))
                        error("JM2005", "Range bounds must be Int.");
                }
                scopes.emplace_back();
                if (item.kind == Statement::Kind::ForRange || item.kind == Statement::Kind::ForEach)
                    declare(item.name,
                            {item.kind == Statement::Kind::ForRange ? Type::Int : Type::Any, false});
                block(item.body, returnType, loops + (item.kind != Statement::Kind::If), function);
                scopes.pop_back();
                scopes.emplace_back();
                block(item.alternative, returnType, loops, function);
                scopes.pop_back();
                break;
            }
            case Statement::Kind::Import:
                if (!item.fileImport &&
                    (item.name.empty() || item.name.front() == '.' || item.name.back() == '.' ||
                     item.name.find("..") != std::string::npos))
                    error("JM2008", "Invalid stable module identity: " + item.name);
                break;
            }
        }
    }
};
} // namespace
bool check(const Program &program, std::vector<Diagnostic> &diagnostics) {
    diagnostics.clear();
    Checker checker(diagnostics);
    for (const auto &statement : program.statements)
        if (statement.kind == Statement::Kind::Function) {
            checker.line = statement.line;
            if (!checker.functions.emplace(statement.name, &statement).second)
                checker.error("JM2004", "Duplicate function: " + statement.name);
        }
    for (const auto &statement : program.statements)
        if (statement.kind == Statement::Kind::Struct)
            checker.structures[statement.name] = &statement;
    checker.block(program.statements);
    return diagnostics.empty();
}
} // namespace jm::script
