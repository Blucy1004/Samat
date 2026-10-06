#pragma once

#include "JMEngine/Script/LanguageCore.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jm::script::ir {

using ValueId = std::uint32_t;
using BlockId = std::uint32_t;

enum class Op {
    Constant,
    Load,
    Store,
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    Negate,
    LogicalNot,
    ToBoolean,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    BooleanAnd,
    BooleanOr,
    Call,
    FloatConstant,
    IntToFloat,
    GlobalLoad,
    GlobalStore,
    FloatToInt,
    BitAnd,
    BitOr,
    BitXor,
    BitNot,
    ShiftLeft,
    ShiftRight,
    StringConstant,
    RuntimeCall
};

struct Instruction {
    Op op{Op::Constant};
    ValueId result{};
    ValueId left{};
    ValueId right{};
    std::int64_t immediate{};
    std::uint32_t local{};
    std::string symbol;
    std::uint64_t symbolId{};
    std::vector<ValueId> arguments;
    Type type{Type::Int};
    double floating{};
    std::vector<std::string> argumentNames;
};

struct Terminator {
    enum class Kind { None, Branch, ConditionalBranch, Return } kind{Kind::None};
    ValueId value{};
    BlockId first{};
    BlockId second{};
};

struct BasicBlock {
    BlockId id{};
    std::string name;
    std::vector<Instruction> instructions;
    Terminator terminator;
};

struct Function {
    std::string name;
    std::uint32_t parameterCount{};
    std::uint32_t localCount{};
    std::uint32_t valueCount{};
    std::vector<BasicBlock> blocks;
    Type returnType{Type::Int};
    std::vector<Type> parameterTypes, localTypes, valueTypes, parameterElementTypes;
    Type returnElementType{Type::Any};
    std::unordered_map<ValueId, Type> valueElementTypes;
};

struct Global {
    std::string name;
    Type type{Type::Int};
    bool constant{};
};
struct EventBinding {
    std::string event, function;
};
struct Module {
    std::vector<Function> functions;
    std::vector<Global> globals;
    std::vector<EventBinding> events;
    std::vector<std::string> imports;
};
struct LoweringDiagnostic {
    std::string message;
};

class NativeFunctionRegistry;
bool lower(const Program &program, Module &output, LoweringDiagnostic &diagnostic,
           const NativeFunctionRegistry *registry = nullptr);
std::string format(const Module &module);
bool verify(const Module &module, LoweringDiagnostic &diagnostic);
Module optimize(const Module &module);

class NativeCode {
  public:
    using Entry = std::int64_t (*)(std::int64_t, std::int64_t, std::int64_t, std::int64_t);
    NativeCode() = default;
    NativeCode(const NativeCode &) = delete;
    NativeCode &operator=(const NativeCode &) = delete;
    NativeCode(NativeCode &&other) noexcept;
    NativeCode &operator=(NativeCode &&other) noexcept;
    ~NativeCode();

    std::int64_t invoke(const std::string &function, const std::vector<std::int64_t> &arguments = {}) const;
    const std::vector<std::uint8_t> &machineCode(const std::string &function) const;
    bool empty() const { return memory_ == nullptr && !invoker_; }
    Value invokeValue(const std::string &function, const std::vector<Value> &arguments = {}) const;

  private:
    friend class X64Backend;
    friend class LLVMBackend;
    void *memory_{};
    std::size_t memorySize_{};
    std::unordered_map<std::string, std::size_t> offsets_;
    std::unordered_map<std::string, std::vector<std::uint8_t>> code_;
    std::unordered_map<std::string, std::uint32_t> arities_;
    std::unordered_map<std::string, Type> returnTypes_;
    std::function<Value(const std::string &, const std::vector<Value> &)> invoker_;
};

class NativeBackend {
  public:
    virtual ~NativeBackend() = default;
    virtual std::string targetTriple() const = 0;
    virtual NativeCode compile(const Module &module, const class NativeFunctionRegistry &registry) const = 0;
};

class NativeFunctionRegistry {
  public:
    struct ParameterEditor {
        std::string label, unit;
        double initial{}, minimum{}, maximum{}, recommendedMinimum{}, recommendedMaximum{}, step{1.0};
        bool numeric{};
        std::vector<std::pair<std::string, double>> presets;
    };
    struct Tooling {
        std::string category, beginnerName, codeTemplate;
        bool advanced{};
        std::vector<ParameterEditor> parameters;
    };
    struct Metadata {
        std::string symbol, displayName, koreanName, documentation;
        std::vector<std::string> parameterNames;
        std::vector<Type> parameterTypes;
        Type returnType{Type::Int};
        std::vector<Type> parameterElementTypes;
        Type returnElementType{Type::Any};
        Tooling tooling{};
    };
    using TypedFunction = std::function<Value(const std::vector<Value> &)>;
    struct TypedBinding {
        Metadata metadata;
        TypedFunction function;
    };
    void registerTypedFunction(Metadata metadata, TypedFunction function);
    const TypedBinding *typed(std::uint64_t symbol) const;
    std::vector<Metadata> allMetadata() const;
    using Function = std::int64_t (*)(std::int64_t, std::int64_t, std::int64_t, std::int64_t);
    void registerFunction(std::string stableSymbol, Function function);
    Function find(std::uint64_t stableSymbolId) const;
    void registerModule(std::string identity);
    bool hasModule(std::string_view identity) const;
    void registerFunction(Metadata metadata, Function function);
    const Metadata *metadata(std::uint64_t stableSymbolId) const;

  private:
    std::unordered_map<std::uint64_t, Function> functions_;
    std::unordered_map<std::uint64_t, Metadata> metadata_;
    std::unordered_set<std::string> modules_;
    std::unordered_map<std::uint64_t, std::shared_ptr<TypedBinding>> typed_;
};

bool metadataMatches(const NativeFunctionRegistry::Metadata &metadata, std::string_view query);
std::string metadataSignature(const NativeFunctionRegistry::Metadata &metadata);
std::string metadataTemplate(const NativeFunctionRegistry::Metadata &metadata, bool korean = false);
void prepareToolingMetadata(NativeFunctionRegistry::Metadata &metadata);

// Optional consumer of JM IR. Builds without LLVM retain all other backends.
class LLVMBackend final : public NativeBackend {
  public:
    explicit LLVMBackend(bool optimized = false, std::string target = {});
    static bool available();
    std::string targetTriple() const override;
    NativeCode compile(const Module &module, const NativeFunctionRegistry &registry = {}) const override;
    std::string emitIR(const Module &module) const;
    void emitObject(const Module &module, const std::string &path, bool entryWrapper = false) const;
    void build(const Module &module, const std::string &output, const std::string &linker = {}) const;

  private:
    bool optimized_{};
    std::string target_;
};

// Bootstrap backend: emits executable x86-64 machine code for the scalar i64 subset.
// The abstract backend boundary keeps LLVM and additional target implementations pluggable.
class X64Backend final : public NativeBackend {
  public:
    std::string targetTriple() const override;
    NativeCode compile(const Module &module, const NativeFunctionRegistry &registry = {}) const override;
};

} // namespace jm::script::ir
