#pragma once

#include "JMEngine/Script/LanguageCore.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace jm::script::ir {

using ValueId = std::uint32_t;
using BlockId = std::uint32_t;

enum class Op { Constant, Load, Store, Add, Subtract, Multiply, Divide, Modulo,
                Negate, LogicalNot, ToBoolean, Equal, NotEqual, Less, LessEqual, Greater,
                GreaterEqual, BooleanAnd, BooleanOr, Call };

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
};

struct Module { std::vector<Function> functions; };
struct LoweringDiagnostic { std::string message; };

bool lower(const Program& program, Module& output, LoweringDiagnostic& diagnostic);
std::string format(const Module& module);

class NativeCode {
public:
    using Entry = std::int64_t(*)(std::int64_t, std::int64_t, std::int64_t, std::int64_t);
    NativeCode() = default;
    NativeCode(const NativeCode&) = delete;
    NativeCode& operator=(const NativeCode&) = delete;
    NativeCode(NativeCode&& other) noexcept;
    NativeCode& operator=(NativeCode&& other) noexcept;
    ~NativeCode();

    std::int64_t invoke(const std::string& function,
                        const std::vector<std::int64_t>& arguments = {}) const;
    const std::vector<std::uint8_t>& machineCode(const std::string& function) const;
    bool empty() const { return memory_ == nullptr; }

private:
    friend class X64Backend;
    void* memory_{};
    std::size_t memorySize_{};
    std::unordered_map<std::string, std::size_t> offsets_;
    std::unordered_map<std::string, std::vector<std::uint8_t>> code_;
};

class NativeBackend {
public:
    virtual ~NativeBackend() = default;
    virtual std::string targetTriple() const = 0;
    virtual NativeCode compile(const Module& module, const class NativeFunctionRegistry& registry) const = 0;
};

class NativeFunctionRegistry {
public:
    using Function = std::int64_t(*)(std::int64_t, std::int64_t, std::int64_t, std::int64_t);
    void registerFunction(std::string stableSymbol, Function function);
    Function find(std::uint64_t stableSymbolId) const;
private:
    std::unordered_map<std::uint64_t, Function> functions_;
};

// Bootstrap backend: emits executable x86-64 machine code for the scalar i64 subset.
// The abstract backend boundary keeps LLVM and additional target implementations pluggable.
class X64Backend final : public NativeBackend {
public:
    std::string targetTriple() const override;
    NativeCode compile(const Module& module, const NativeFunctionRegistry& registry = {}) const override;
};

} // namespace jm::script::ir
