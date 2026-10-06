#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace jm::script {
namespace ir {
class NativeFunctionRegistry;
}

enum class Type {
    Any,
    Int,
    Float,
    Bool,
    String,
    Void,
    List,
    Map,
    Vector2,
    Vector3,
    Color,
    Enum,
    Struct,
    Tuple,
    Range,
    Optional,
    Entity,
    Null
};
std::string typeName(Type type);
Type parseType(std::string_view name);
struct TypeAnnotation {
    Type base{Type::Any}, element{Type::Any};
    std::string nominal{};
};
TypeAnnotation parseAnnotation(std::string_view text);
std::string annotationName(Type base, Type element = Type::Any, std::string_view nominal = {});

struct Vector2Value {
    double x{0.0};
    double y{0.0};
};
struct Vector3Value {
    double x{}, y{}, z{};
};
struct ColorValue {
    double r{0.0};
    double g{0.0};
    double b{0.0};
    double a{1.0};
};
struct EntityReference {
    std::string id;
    std::uint64_t sceneIdentity{}, generation{};
};

struct StructObject;
struct OptionalValue;
struct RangeValue {
    std::int64_t start{}, end{}, step{1};
};
struct EnumValue {
    std::string type, member;
    std::int64_t ordinal{};
};
struct Value {
    struct Array;
    struct Tuple;
    using TuplePtr = std::shared_ptr<Tuple>;
    struct Map;
    using StructPtr = std::shared_ptr<StructObject>;
    using ArrayPtr = std::shared_ptr<Array>;
    using MapPtr = std::shared_ptr<Map>;
    using Storage = std::variant<std::monostate, bool, double, std::string, ArrayPtr, MapPtr, Vector2Value,
                                 ColorValue, EntityReference, std::int64_t, Vector3Value, EnumValue,
                                 StructPtr, TuplePtr, RangeValue, std::shared_ptr<OptionalValue>>;
    Storage data;

    Value() = default;
    Value(bool value) : data(value) {}
    Value(double value) : data(value) {}
    Value(int value) : data(static_cast<std::int64_t>(value)) {}
    Value(std::int64_t value) : data(value) {}
    Value(std::string value) : data(std::move(value)) {}
    Value(const char *value) : data(std::string(value)) {}
    Value(Vector2Value value) : data(value) {}
    Value(TuplePtr value) : data(std::move(value)) {}
    Value(RangeValue value) : data(value) {}
    Value(EnumValue value) : data(std::move(value)) {}
    Value(StructPtr value) : data(std::move(value)) {}
    Value(Vector3Value value) : data(value) {}
    Value(ColorValue value) : data(value) {}
    Value(EntityReference value) : data(std::move(value)) {}

    static Value optional(Type element, Value value = {}, std::string nominal = {});
    Value unwrap() const;
    static Value array(Array value);
    static Value map(Map value);
    bool isNull() const;
    std::string typeName() const;
    std::string toString() const;
    Type type() const;
};

struct OptionalValue {
    Type element;
    Value value;
    std::string nominal;
    std::map<std::string, TypeAnnotation> schema;
};

struct Value::Array : std::vector<Value> {
    using std::vector<Value>::vector;
    Type elementType{Type::Any};
};

struct Value::Tuple {
    Array values;
};
struct Value::Map : std::map<std::string, Value> {
    using std::map<std::string, Value>::map;
    bool immutable{false};
    Type elementType{Type::Any};
};
struct StructObject {
    std::string name;
    Value::MapPtr fields;
    std::map<std::string, TypeAnnotation> schema;
};

struct Expression;
using ExpressionPtr = std::shared_ptr<Expression>;

struct Expression {
    enum class Kind { Literal, Identifier, Unary, Binary, Array, Map, Index, Member, Call, Tuple };
    struct NamedArgument {
        std::string name;
        ExpressionPtr value;
    };
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
    enum class Kind {
        Variable,
        Assignment,
        Expression,
        If,
        While,
        ForRange,
        ForEach,
        Function,
        Return,
        Break,
        Continue,
        Import,
        Event,
        Enum,
        Struct
    };
    Kind kind{Kind::Expression};
    std::string name;
    std::string operation;
    bool constant{false};
    bool fileImport{false};
    ExpressionPtr target;
    ExpressionPtr expression;
    ExpressionPtr rangeEnd;
    std::vector<std::string> parameters;
    StatementList body;
    StatementList alternative;
    std::string declaredTypeName, returnTypeName;
    std::vector<std::string> parameterTypeNames;
    Type declaredType{Type::Any};
    Type returnType{Type::Any};
    Type elementType{Type::Any}, returnElementType{Type::Any};
    std::vector<Type> parameterTypes, parameterElementTypes;
    std::size_t line{};
};

struct Program {
    StatementList statements;
};

struct Diagnostic {
    Diagnostic(std::string text = {}, std::size_t sourceLine = 0)
        : message(std::move(text)), line(sourceLine) {}
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
    void declare(const std::string &name, Value value, bool constant = false);
    void assign(const std::string &name, Value value);
    Value get(const std::string &name) const;
    bool containsLocal(const std::string &name) const;
    bool contains(const std::string &name) const;
    std::shared_ptr<Environment> child();
    void eraseLocal(const std::string &name);
    std::map<std::string, std::string> inspect(std::size_t limit = 64) const;

  private:
    struct Binding {
        Value value;
        bool constant{false};
    };
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
    std::shared_ptr<const ir::NativeFunctionRegistry> nativeMetadata;
};

struct ExecutionResult {
    std::shared_ptr<Environment> globals;
    std::vector<std::string> output;
    std::size_t instructionsExecuted{0};
    Value returnValue;
};

using HostFunction =
    std::function<Value(const std::string &, const std::vector<Value> &, const std::vector<std::string> &)>;

class ExecutionSession {
  public:
    ExecutionSession(Program program, RunOptions options = {}, HostFunction host = {});
    ~ExecutionSession();
    ExecutionSession(const ExecutionSession &) = delete;
    ExecutionSession &operator=(const ExecutionSession &) = delete;
    ExecutionResult dispatch(const std::string &event);
    ExecutionResult invoke(const std::string &entry);
    ExecutionResult evaluate(const std::string &source);
    bool hotSwap(Program candidate, Diagnostic &diagnostic);
    std::uint64_t generation() const;
    std::map<std::string, std::string> inspect() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

bool hotSwapCompatible(const Program &running, const Program &candidate, Diagnostic &diagnostic,
                       const ir::NativeFunctionRegistry *registry = nullptr);
ExpressionPtr parseExpression(const std::string &source, std::string *error = nullptr);
std::uint64_t stableBuiltinSymbolId(std::string_view name);
Value evaluateExpression(const ExpressionPtr &expression, const Environment &environment,
                         const HostFunction &hostFunction = {});
Value evaluateExpression(const std::string &source, const Environment &environment,
                         const HostFunction &hostFunction = {});
bool parseCode(const std::string &source, Program &output, Diagnostic &diagnostic);
bool parseKorean(const std::string &source, Program &output, Diagnostic &diagnostic);
bool structurallyEqual(const Program &left, const Program &right);
bool check(const Program &program, std::vector<Diagnostic> &diagnostics,
           const ir::NativeFunctionRegistry *registry = nullptr);
std::string renderCode(const Program &program);
std::string renderKorean(const Program &program);
ExecutionResult execute(const Program &program, const RunOptions &options = {},
                        const HostFunction &hostFunction = {});

} // namespace jm::script
