#pragma once

#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <cstdint>
#include <unordered_map>
#include <variant>
#include <vector>

namespace jm::script {

enum class Type { Any, Int, Float, Bool, String, Void, List, Map, Vector2 };
std::string typeName(Type type);
Type parseType(std::string_view name);

struct Vector2Value { double x{0.0}; double y{0.0}; };
struct ColorValue { double r{0.0}; double g{0.0}; double b{0.0}; double a{1.0}; };
struct EntityReference { std::string id; };

struct Value {
    using Array = std::vector<Value>;
    using Map = std::map<std::string, Value>;
    using ArrayPtr = std::shared_ptr<Array>;
    using MapPtr = std::shared_ptr<Map>;
    using Storage = std::variant<std::monostate, bool, double, std::string, ArrayPtr, MapPtr,
                                 Vector2Value, ColorValue, EntityReference, std::int64_t>;
    Storage data;

    Value() = default;
    Value(bool value) : data(value) {}
    Value(double value) : data(value) {}
    Value(int value) : data(static_cast<std::int64_t>(value)) {}
    Value(std::int64_t value) : data(value) {}
    Value(std::string value) : data(std::move(value)) {}
    Value(const char* value) : data(std::string(value)) {}
    Value(Vector2Value value) : data(value) {}
    Value(ColorValue value) : data(value) {}
    Value(EntityReference value) : data(std::move(value)) {}

    static Value array(Array value);
    static Value map(Map value);
    bool isNull() const;
    std::string typeName() const;
    std::string toString() const;
    Type type() const;
};

struct Expression;
using ExpressionPtr = std::shared_ptr<Expression>;

struct Expression {
    enum class Kind { Literal, Identifier, Unary, Binary, Array, Map, Index, Member, Call };
    struct NamedArgument { std::string name; ExpressionPtr value; };
    Kind kind{Kind::Literal};
    Value literal;
    std::string text;
    ExpressionPtr left;
    ExpressionPtr right;
    std::vector<ExpressionPtr> elements;
    std::vector<std::pair<std::string, ExpressionPtr>> entries;
    std::vector<NamedArgument> arguments;
    std::uint64_t builtinSymbolId{};
    std::string builtinSymbolName;
};

struct Statement;
using StatementList = std::vector<Statement>;

struct Statement {
    enum class Kind { Variable, Assignment, Expression, If, While, ForRange, ForEach,
                      Function, Return, Break, Continue, Import, Event };
    Kind kind{Kind::Expression};
    std::string name;
    std::string operation;
    bool constant{false};
    ExpressionPtr target;
    ExpressionPtr expression;
    ExpressionPtr rangeEnd;
    std::vector<std::string> parameters;
    StatementList body;
    StatementList alternative;
    Type declaredType{Type::Any};
    Type returnType{Type::Any};
    std::vector<Type> parameterTypes;
    std::size_t line{};
};

struct Program { StatementList statements; };

struct Diagnostic {
    Diagnostic(std::string text = {}, std::size_t sourceLine = 0) : message(std::move(text)), line(sourceLine) {}
    std::string message;
    std::size_t line{0};
    std::string code{"JM1001"};
    enum class Severity { Error, Warning } severity{Severity::Error};
    std::size_t column{1};
    std::string expected, actual, suggestion, developerDetail;
};

class Environment : public std::enable_shared_from_this<Environment> {
public:
    explicit Environment(std::shared_ptr<Environment> parent = {});
    void declare(const std::string& name, Value value, bool constant = false);
    void assign(const std::string& name, Value value);
    Value get(const std::string& name) const;
    bool containsLocal(const std::string& name) const;
    std::shared_ptr<Environment> child();

private:
    struct Binding { Value value; bool constant{false}; };
    std::unordered_map<std::string, Binding> bindings_;
    std::shared_ptr<Environment> parent_;
};

struct RunOptions {
    std::size_t instructionBudget{1'000'000};
    std::size_t recursionLimit{256};
    std::function<bool()> shouldStop;
    std::unordered_map<std::string, Value> initialValues;
    std::string entryFunction;
    std::string eventName;
    std::function<bool(std::string_view)> moduleResolver;
};

struct ExecutionResult {
    std::shared_ptr<Environment> globals;
    std::vector<std::string> output;
    std::size_t instructionsExecuted{0};
    Value returnValue;
};

using HostFunction = std::function<Value(const std::string&, const std::vector<Value>&,
                                         const std::vector<std::string>&)>;

ExpressionPtr parseExpression(const std::string& source, std::string* error = nullptr);
std::uint64_t stableBuiltinSymbolId(std::string_view name);
Value evaluateExpression(const ExpressionPtr& expression, const Environment& environment,
                         const HostFunction& hostFunction = {});
Value evaluateExpression(const std::string& source, const Environment& environment,
                         const HostFunction& hostFunction = {});
bool parseCode(const std::string& source, Program& output, Diagnostic& diagnostic);
bool parseKorean(const std::string& source, Program& output, Diagnostic& diagnostic);
bool structurallyEqual(const Program& left, const Program& right);
bool check(const Program& program, std::vector<Diagnostic>& diagnostics);
std::string renderCode(const Program& program);
std::string renderKorean(const Program& program);
ExecutionResult execute(const Program& program, const RunOptions& options = {},
                        const HostFunction& hostFunction = {});

} // namespace jm::script
