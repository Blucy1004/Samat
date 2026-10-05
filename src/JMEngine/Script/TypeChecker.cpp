#include "JMEngine/Script/LanguageCore.hpp"
#include <algorithm>
#include <unordered_set>

namespace jm::script {
std::string typeName(Type type) {
    static const char* names[]{"Any", "Int", "Float", "Bool", "String", "Void", "List", "Map", "Vector2"};
    return names[static_cast<unsigned>(type)];
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
    };
    std::vector<std::unordered_map<std::string, Binding>> scopes{1};
    std::unordered_map<std::string, const Statement*> functions;
    std::vector<Diagnostic>& errors;
    explicit Checker(std::vector<Diagnostic>& diagnostics) : errors(diagnostics) {}
    std::size_t line{};
    void error(std::string code, std::string message, Type expected = Type::Any, Type actual = Type::Any) {
        Diagnostic diagnostic{std::move(message), line};
        diagnostic.code = std::move(code);
        diagnostic.expected = typeName(expected);
        diagnostic.actual = typeName(actual);
        diagnostic.suggestion = "Check the declaration and operand types.";
        errors.push_back(std::move(diagnostic));
    }
    Binding lookup(const std::string& name) const {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
            if (auto it = scope->find(name); it != scope->end())
                return it->second;
        return {Type::Any, false}; // Host values can be injected at execution time.
    }
    void declare(const std::string& name, Binding binding) {
        if (!scopes.back().emplace(name, binding).second)
            error("JM2004", "Duplicate declaration: " + name);
    }
    Type expr(const ExpressionPtr& value) {
        if (!value)
            return Type::Void;
        switch (value->kind) {
        case Expression::Kind::Literal:
            return value->literal.type();
        case Expression::Kind::Identifier:
            return lookup(value->text).type;
        case Expression::Kind::Array:
            for (const auto& item : value->elements)
                expr(item);
            return Type::List;
        case Expression::Kind::Map:
            for (const auto& item : value->entries)
                expr(item.second);
            return Type::Map;
        case Expression::Kind::Index: {
            auto container = expr(value->left), index = expr(value->right);
            if ((container == Type::List || container == Type::String) && index != Type::Int &&
                index != Type::Any)
                error("JM2005", "List/string index must be Int.", Type::Int, index);
            return container == Type::String ? Type::String : Type::Any;
        }
        case Expression::Kind::Member:
            expr(value->left);
            return value->text == "length" ? Type::Int : Type::Any;
        case Expression::Kind::Unary: {
            auto operand = expr(value->right);
            if (value->text == "!" || value->text == "not")
                return Type::Bool;
            if (!numeric(operand))
                error("JM2006", "Unary arithmetic requires a numeric operand.", Type::Int, operand);
            return operand;
        }
        case Expression::Kind::Binary: {
            auto left = expr(value->left), right = expr(value->right);
            const auto& op = value->text;
            if (op == "and" || op == "or" || op == "&&" || op == "||" || op == "==" || op == "!=")
                return Type::Bool;
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
            return left == Type::Float || right == Type::Float || op == "^" ? Type::Float
                   : left == Type::Any || right == Type::Any                ? Type::Any
                                                                            : Type::Int;
        }
        case Expression::Kind::Call: {
            std::vector<Type> arguments;
            for (const auto& item : value->arguments)
                arguments.push_back(expr(item.value));
            if (!value->left || value->left->kind != Expression::Kind::Identifier) {
                expr(value->left);
                return Type::Any;
            }
            const auto& name = value->left->text;
            if (auto found = functions.find(name); found != functions.end()) {
                const auto& fn = *found->second;
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
            if (name == "print" || name == "println")
                return Type::Void;
            if (name == "len" || name == "length")
                return Type::Int;
            if (name == "assert")
                return Type::Bool;
            if (name == "vector2")
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
    bool returns(const StatementList& list) const {
        for (const auto& item : list) {
            if (item.kind == Statement::Kind::Return)
                return true;
            if (item.kind == Statement::Kind::If && returns(item.body) && returns(item.alternative))
                return true;
        }
        return false;
    }
    void block(const StatementList& list, Type returnType = Type::Any, int loops = 0, bool function = false) {
        for (const auto& item : list) {
            line = item.line;
            switch (item.kind) {
            case Statement::Kind::Variable: {
                auto actual = item.expression ? expr(item.expression) : Type::Any;
                if(actual == Type::Void && item.declaredType == Type::Any) actual = Type::Any;
                if (item.declaredType == Type::Void)
                    error("JM2001", "Variables cannot have type Void.");
                if (item.expression && !accepts(item.declaredType, actual))
                    error("JM2001",
                          "Variable '" + item.name + "' requires " + typeName(item.declaredType) + ", got " +
                              typeName(actual) + ".",
                          item.declaredType, actual);
                declare(item.name,
                        {item.declaredType == Type::Any ? actual : item.declaredType, item.constant});
                break;
            }
            case Statement::Kind::Assignment: {
                if(!item.target || (item.target->kind!=Expression::Kind::Identifier && item.target->kind!=Expression::Kind::Index && item.target->kind!=Expression::Kind::Member)) error("JM2001","Assignment requires a variable or collection member.");
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
                if (function)
                    error("JM2009", "Event handlers must be declared at module scope.");
                scopes.emplace_back();
                block(item.body, Type::Void, 0, true);
                scopes.pop_back();
                break;
            }
            case Statement::Kind::Function: {
                if (function)
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
                if(item.name.empty() || item.name.front()=='.' || item.name.back()=='.' || item.name.find("..")!=std::string::npos)error("JM2008","Invalid stable module identity: "+item.name);
                break;
            }
        }
    }
};
} // namespace
bool check(const Program& program, std::vector<Diagnostic>& diagnostics) {
    diagnostics.clear();
    Checker checker(diagnostics);
    for (const auto& statement : program.statements)
        if (statement.kind == Statement::Kind::Function) {
            checker.line = statement.line;
            if (!checker.functions.emplace(statement.name, &statement).second)
                checker.error("JM2004", "Duplicate function: " + statement.name);
        }
    checker.block(program.statements);
    return diagnostics.empty();
}
} // namespace jm::script
