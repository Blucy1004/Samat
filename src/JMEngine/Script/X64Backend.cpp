#include "JMEngine/Script/JMIR.hpp"
#include "JMEngine/Script/StandardLibrary.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace jm::script::ir {
namespace {

thread_local int arithmeticError{};
std::int64_t checkedDivide(std::int64_t a, std::int64_t b, std::int64_t remainder, std::int64_t) noexcept {
    if (b == 0) {
        arithmeticError = 1;
        return 0;
    }
    if (a == std::numeric_limits<std::int64_t>::min() && b == -1) {
        arithmeticError = 2;
        return 0;
    }
    return remainder ? a % b : a / b;
}
std::int64_t checkedShift(std::int64_t a, std::int64_t b, std::int64_t right, std::int64_t) noexcept {
    if (b < 0 || b >= 64) {
        arithmeticError = 4;
        return 0;
    }
    auto bits = static_cast<std::uint64_t>(a);
    return std::bit_cast<std::int64_t>(
        right ? ((bits >> b) | (a < 0 && b ? (~std::uint64_t{0} << (64 - b)) : 0)) : (bits << b));
}
std::int64_t faultStatus(std::int64_t, std::int64_t, std::int64_t, std::int64_t) noexcept {
    return arithmeticError;
}
using Bytes = std::vector<std::uint8_t>;
void u32(Bytes &out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xff));
}
void u64(Bytes &out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xff));
}
void patch32(Bytes &out, std::size_t at, std::int64_t value) {
    if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max())
        throw std::runtime_error("Native branch/call is too far for rel32.");
    const auto v = static_cast<std::uint32_t>(static_cast<std::int32_t>(value));
    for (int i = 0; i < 4; ++i)
        out.at(at + static_cast<std::size_t>(i)) = static_cast<std::uint8_t>((v >> (i * 8)) & 0xff);
}
std::int32_t disp(std::uint32_t slot) { return -static_cast<std::int32_t>((slot + 1U) * 8U); }
void memRbp(Bytes &out, std::initializer_list<std::uint8_t> opcode, std::uint32_t slot) {
    out.insert(out.end(), opcode.begin(), opcode.end());
    u32(out, static_cast<std::uint32_t>(disp(slot)));
}

struct Pending {
    std::size_t at;
    std::string symbol;
};
struct CompiledFunction {
    std::size_t start{};
    std::size_t size{};
    std::uint32_t parameterCount{};
    std::unordered_map<BlockId, std::size_t> blocks;
    std::vector<std::pair<std::size_t, BlockId>> jumps;
};

void emitLoadValue(Bytes &code, const Function &f, ValueId value) {
    memRbp(code, {0x48, 0x8b, 0x85}, f.localCount + value);
}
void emitStoreValue(Bytes &code, const Function &f, ValueId value) {
    memRbp(code, {0x48, 0x89, 0x85}, f.localCount + value);
}

void emitFaultCheck(Bytes &code) {
    code.insert(code.end(), {0x48, 0x83, 0xec, 0x20, 0x48, 0xb8});
    u64(code, reinterpret_cast<std::uintptr_t>(&faultStatus));
    code.insert(code.end(),
                {0xff, 0xd0, 0x48, 0x83, 0xc4, 0x20, 0x48, 0x85, 0xc0, 0x74, 0x04, 0x31, 0xc0, 0xc9, 0xc3});
}
void validateCall(const Instruction &in, const NativeFunctionRegistry &registry, const Function &fn) {
    const auto *info = registry.metadata(in.symbolId);
    if (info && info->parameterTypes.size() != in.arguments.size())
        throw std::runtime_error("Native registry argument count mismatch: " + in.symbol);
    if (info) {
        if (info->returnType != in.type)
            throw std::runtime_error("Native registry return type does not match JM IR: " + in.symbol);
        for (std::size_t i = 0; i < in.arguments.size(); ++i)
            if (fn.valueTypes.at(in.arguments[i]) != info->parameterTypes[i])
                throw std::runtime_error("Native registry parameter type mismatch: " + in.symbol);
    }
    for (std::size_t i = 0; i < in.argumentNames.size(); ++i)
        if (!in.argumentNames[i].empty() && (!info || in.argumentNames[i] != info->parameterNames[i]))
            throw std::runtime_error("Native named arguments require matching registry metadata order: " +
                                     in.symbol);
}
CompiledFunction emitFunction(Bytes &code, const Function &f, std::vector<Pending> &calls,
                              const NativeFunctionRegistry &registry) {
    CompiledFunction compiled;
    compiled.start = code.size();
    compiled.parameterCount = f.parameterCount;
    code.push_back(0x55);
    code.insert(code.end(), {0x48, 0x89, 0xe5});
    code.insert(code.end(), {0x48, 0x83, 0xe4, 0xf0}); // align stack for the Windows x64 C++ ABI
    const std::uint32_t slots = f.localCount + f.valueCount;
    const std::uint32_t stackSize = ((slots * 8U + 15U) / 16U) * 16U;
    if (stackSize) {
        code.insert(code.end(), {0x48, 0x81, 0xec});
        u32(code, stackSize);
    }
    for (std::uint32_t i = 0; i < f.parameterCount; ++i) {
        // Internal JM calling convention: arguments are pushed right-to-left.
        code.insert(code.end(), {0x48, 0x8b, 0x85});
        u32(code, 16U + i * 8U);
        memRbp(code, {0x48, 0x89, 0x85}, i);
    }
    std::vector<std::pair<std::size_t, BlockId>> localJumps;
    for (const BasicBlock &block : f.blocks) {
        compiled.blocks[block.id] = code.size();
        for (const Instruction &in : block.instructions) {
            switch (in.op) {
            case Op::Constant:
                code.insert(code.end(), {0x48, 0xb8});
                u64(code, static_cast<std::uint64_t>(in.immediate));
                emitStoreValue(code, f, in.result);
                break;
            case Op::Load:
                memRbp(code, {0x48, 0x8b, 0x85}, in.local);
                emitStoreValue(code, f, in.result);
                break;
            case Op::Store:
                emitLoadValue(code, f, in.left);
                memRbp(code, {0x48, 0x89, 0x85}, in.local);
                break;
            case Op::BitNot:
                emitLoadValue(code, f, in.left);
                code.insert(code.end(), {0x48, 0xf7, 0xd0});
                emitStoreValue(code, f, in.result);
                break;
            case Op::Negate:
                emitLoadValue(code, f, in.left);
                code.insert(code.end(), {0x48, 0xf7, 0xd8});
                emitStoreValue(code, f, in.result);
                break;
            case Op::LogicalNot:
                emitLoadValue(code, f, in.left);
                code.insert(code.end(), {0x48, 0x85, 0xc0, 0x0f, 0x94, 0xc0, 0x48, 0x0f, 0xb6, 0xc0});
                emitStoreValue(code, f, in.result);
                break;
            case Op::ToBoolean:
                emitLoadValue(code, f, in.left);
                code.insert(code.end(), {0x48, 0x85, 0xc0, 0x0f, 0x95, 0xc0, 0x48, 0x0f, 0xb6, 0xc0});
                emitStoreValue(code, f, in.result);
                break;
            case Op::Call:
                if (const auto external = registry.find(in.symbolId)) {
                    validateCall(in, registry, f);
                    if (in.arguments.size() > 4)
                        throw std::runtime_error(
                            "Native C++ interop currently supports up to four i64 arguments.");
                    for (std::size_t i = 0; i < in.arguments.size(); ++i) {
                        emitLoadValue(code, f, in.arguments[i]);
#if defined(_WIN32)
                        if (i == 0)
                            code.insert(code.end(), {0x48, 0x89, 0xc1});
                        else if (i == 1)
                            code.insert(code.end(), {0x48, 0x89, 0xc2});
                        else if (i == 2)
                            code.insert(code.end(), {0x49, 0x89, 0xc0});
                        else
                            code.insert(code.end(), {0x49, 0x89, 0xc1});
#else
                        if (i == 0)
                            code.insert(code.end(), {0x48, 0x89, 0xc7});
                        else if (i == 1)
                            code.insert(code.end(), {0x48, 0x89, 0xc6});
                        else if (i == 2)
                            code.insert(code.end(), {0x48, 0x89, 0xc2});
                        else
                            code.insert(code.end(), {0x48, 0x89, 0xc1});
#endif
                    }
                    code.insert(code.end(), {0x48, 0x83, 0xec, 0x20, 0x48, 0xb8});
                    u64(code, reinterpret_cast<std::uintptr_t>(external));
                    code.insert(code.end(), {0xff, 0xd0, 0x48, 0x83, 0xc4, 0x20});
                    emitStoreValue(code, f, in.result);
                    emitFaultCheck(code);
                    break;
                }
                for (auto it = in.arguments.rbegin(); it != in.arguments.rend(); ++it) {
                    const auto d = disp(f.localCount + *it);
                    code.insert(code.end(), {0xff, 0xb5});
                    u32(code, static_cast<std::uint32_t>(d));
                }
                code.push_back(0xe8);
                calls.push_back({code.size(), in.symbol});
                u32(code, 0);
                if (!in.arguments.empty()) {
                    code.insert(code.end(), {0x48, 0x81, 0xc4});
                    u32(code, static_cast<std::uint32_t>(in.arguments.size() * 8U));
                }
                emitStoreValue(code, f, in.result);
                emitFaultCheck(code);
                break;
            default: {
                emitLoadValue(code, f, in.left);
                code.insert(code.end(), {0x48, 0x89, 0xc1});
                emitLoadValue(code, f, in.right);
                code.push_back(0x48);
                code.push_back(0x91); // xchg rax, rcx -> left in rax, right in rcx
                switch (in.op) {
                case Op::BitAnd:
                    code.insert(code.end(), {0x48, 0x21, 0xc8});
                    break;
                case Op::BitOr:
                    code.insert(code.end(), {0x48, 0x09, 0xc8});
                    break;
                case Op::BitXor:
                    code.insert(code.end(), {0x48, 0x31, 0xc8});
                    break;
                case Op::Add:
                    code.insert(code.end(), {0x48, 0x01, 0xc8});
                    break;
                case Op::Subtract:
                    code.insert(code.end(), {0x48, 0x29, 0xc8});
                    break;
                case Op::Multiply:
                    code.insert(code.end(), {0x48, 0x0f, 0xaf, 0xc1});
                    break;
                case Op::ShiftLeft:
                case Op::ShiftRight:
                case Op::Divide:
                case Op::Modulo:
#if defined(_WIN32)
                    code.insert(code.end(), {0x48, 0x89, 0xca, 0x48, 0x89, 0xc1, 0x49, 0xc7, 0xc0});
                    u32(code, in.op == Op::Modulo || in.op == Op::ShiftRight ? 1 : 0);
                    code.insert(code.end(), {0x45, 0x31, 0xc9});
#else
                    code.insert(code.end(), {0x48, 0x89, 0xc7, 0x48, 0x89, 0xce, 0xba});
                    u32(code, in.op == Op::Modulo || in.op == Op::ShiftRight ? 1 : 0);
                    code.insert(code.end(), {0x31, 0xc9});
#endif
                    code.insert(code.end(), {0x48, 0x83, 0xec, 0x20, 0x48, 0xb8});
                    u64(code, reinterpret_cast<std::uintptr_t>(
                                  in.op == Op::ShiftLeft || in.op == Op::ShiftRight ? &checkedShift
                                                                                    : &checkedDivide));
                    code.insert(code.end(), {0xff, 0xd0, 0x48, 0x83, 0xc4, 0x20});
                    break;
                case Op::BooleanAnd:
                    code.insert(code.end(), {0x48, 0x85, 0xc0, 0x0f, 0x95, 0xc0, 0x48, 0x85, 0xc9, 0x0f, 0x95,
                                             0xc1, 0x20, 0xc8, 0x0f, 0xb6, 0xc0});
                    break;
                case Op::BooleanOr:
                    code.insert(code.end(), {0x48, 0x85, 0xc0, 0x0f, 0x95, 0xc0, 0x48, 0x85, 0xc9, 0x0f, 0x95,
                                             0xc1, 0x08, 0xc8, 0x0f, 0xb6, 0xc0});
                    break;
                case Op::Equal:
                case Op::NotEqual:
                case Op::Less:
                case Op::LessEqual:
                case Op::Greater:
                case Op::GreaterEqual: {
                    code.insert(code.end(), {0x48, 0x39, 0xc8});
                    const std::uint8_t cc = in.op == Op::Equal       ? 0x94
                                            : in.op == Op::NotEqual  ? 0x95
                                            : in.op == Op::Less      ? 0x9c
                                            : in.op == Op::LessEqual ? 0x9e
                                            : in.op == Op::Greater   ? 0x9f
                                                                     : 0x9d;
                    code.insert(code.end(), {0x0f, cc, 0xc0, 0x48, 0x0f, 0xb6, 0xc0});
                    break;
                }
                default:
                    throw std::runtime_error("Invalid x64 IR instruction.");
                }
                emitStoreValue(code, f, in.result);
                if (in.op == Op::ShiftLeft || in.op == Op::ShiftRight || in.op == Op::Divide ||
                    in.op == Op::Modulo)
                    emitFaultCheck(code);
                break;
            }
            }
        }
        const Terminator &term = block.terminator;
        if (term.kind == Terminator::Kind::Return) {
            emitLoadValue(code, f, term.value);
            code.insert(code.end(), {0xc9, 0xc3});
        } else if (term.kind == Terminator::Kind::Branch) {
            code.push_back(0xe9);
            localJumps.emplace_back(code.size(), term.first);
            u32(code, 0);
        } else if (term.kind == Terminator::Kind::ConditionalBranch) {
            emitLoadValue(code, f, term.value);
            code.insert(code.end(), {0x48, 0x85, 0xc0, 0x0f, 0x85});
            localJumps.emplace_back(code.size(), term.first);
            u32(code, 0);
            code.push_back(0xe9);
            localJumps.emplace_back(code.size(), term.second);
            u32(code, 0);
        } else
            throw std::runtime_error("JM IR block has no terminator.");
    }
    for (const auto &[patchAt, target] : localJumps)
        patch32(code, patchAt,
                static_cast<std::int64_t>(compiled.blocks.at(target)) -
                    static_cast<std::int64_t>(patchAt + 4));
    compiled.size = code.size() - compiled.start;
    return compiled;
}

void *allocateExecutable(std::size_t size) {
#if defined(_WIN32)
    return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#elif defined(__unix__) || defined(__APPLE__)
    void *p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
#else
    (void)size;
    return nullptr;
#endif
}
void protectExecutable(void *p, std::size_t size) {
#if defined(_WIN32)
    DWORD old{};
    if (!VirtualProtect(p, size, PAGE_EXECUTE_READ, &old))
        throw std::runtime_error("Could not make native code executable.");
    FlushInstructionCache(GetCurrentProcess(), p, size);
#elif defined(__unix__) || defined(__APPLE__)
    if (mprotect(p, size, PROT_READ | PROT_EXEC) != 0)
        throw std::runtime_error("Could not make native code executable.");
#endif
}
void freeExecutable(void *p, std::size_t size) {
#if defined(_WIN32)
    (void)size;
    if (p)
        VirtualFree(p, 0, MEM_RELEASE);
#elif defined(__unix__) || defined(__APPLE__)
    if (p)
        munmap(p, size);
#else
    (void)p;
    (void)size;
#endif
}

} // namespace

NativeCode::NativeCode(NativeCode &&other) noexcept { *this = std::move(other); }
NativeCode &NativeCode::operator=(NativeCode &&other) noexcept {
    if (this != &other) {
        freeExecutable(memory_, memorySize_);
        memory_ = std::exchange(other.memory_, nullptr);
        memorySize_ = std::exchange(other.memorySize_, 0);
        offsets_ = std::move(other.offsets_);
        code_ = std::move(other.code_);
        arities_ = std::move(other.arities_);
        returnTypes_ = std::move(other.returnTypes_);
        invoker_ = std::move(other.invoker_);
    }
    return *this;
}
NativeCode::~NativeCode() { freeExecutable(memory_, memorySize_); }
std::int64_t NativeCode::invoke(const std::string &function, const std::vector<std::int64_t> &args) const {
    if (invoker_) {
        std::vector<Value> values;
        for (auto argument : args)
            values.emplace_back(argument);
        const auto result = invoker_(function, values);
        if (result.type() == Type::Int)
            return std::get<std::int64_t>(result.data);
        if (result.type() == Type::Bool)
            return std::get<bool>(result.data) ? 1 : 0;
        throw std::runtime_error("Use invokeValue for a non-integer native result.");
    }
    if (arities_.contains(function) && args.size() != arities_.at(function))
        throw std::runtime_error("Native invocation argument count mismatch: " + function);
    if (args.size() > 4)
        throw std::runtime_error("JIT entry invocation supports up to four arguments.");
    const auto found = offsets_.find(function);
    if (found == offsets_.end())
        throw std::runtime_error("Native function not found: " + function);
    auto entry = reinterpret_cast<Entry>(static_cast<std::uint8_t *>(memory_) + found->second);
    std::int64_t a[4]{};
    std::copy(args.begin(), args.end(), a);
    arithmeticError = 0;
    const auto result = entry(a[0], a[1], a[2], a[3]);
    if (arithmeticError)
        throw std::runtime_error(arithmeticError == 1   ? "JM3001: Cannot divide by zero."
                                 : arithmeticError == 4 ? "JM3004: Shift count must be in 0..63."
                                                        : "JM3002: Integer division overflow.");
    return result;
}
void NativeFunctionRegistry::registerFunction(std::string stableSymbol, Function function) {
    if (stableSymbol.rfind("builtin.", 0) != 0 || !function)
        throw std::runtime_error("Native function registrations need a non-null builtin.* stable symbol.");
    const auto id = stableBuiltinSymbolId(stableSymbol);
    if (!functions_.emplace(id, function).second)
        throw std::runtime_error("Native builtin symbol ID is already registered.");
}
void NativeFunctionRegistry::registerModule(std::string identity) {
    if (identity.empty())
        throw std::runtime_error("A module needs a stable nonempty identity.");
    modules_.insert(std::move(identity));
}
bool NativeFunctionRegistry::hasModule(std::string_view identity) const {
    return modules_.contains(std::string(identity));
}
NativeFunctionRegistry::Function NativeFunctionRegistry::find(std::uint64_t stableSymbolId) const {
    const auto found = functions_.find(stableSymbolId);
    return found == functions_.end() ? nullptr : found->second;
}
const std::vector<std::uint8_t> &NativeCode::machineCode(const std::string &function) const {
    const auto found = code_.find(function);
    if (found == code_.end())
        throw std::runtime_error("Native function not found: " + function);
    return found->second;
}
Value NativeCode::invokeValue(const std::string &function, const std::vector<Value> &arguments) const {
    if (invoker_)
        return invoker_(function, arguments);
    std::vector<std::int64_t> values;
    for (const auto &value : arguments) {
        if (value.type() == Type::Int)
            values.push_back(std::get<std::int64_t>(value.data));
        else if (value.type() == Type::Bool)
            values.push_back(std::get<bool>(value.data) ? 1 : 0);
        else
            throw std::runtime_error("Bootstrap native invocation requires Int/Bool arguments.");
    }
    const auto result = invoke(function, values);
    if (returnTypes_.contains(function) && returnTypes_.at(function) == Type::Bool)
        return Value(result != 0);
    if (returnTypes_.contains(function) && returnTypes_.at(function) == Type::Void)
        return Value{};
    return Value(result);
}
void NativeFunctionRegistry::registerFunction(Metadata info, Function function) {
    if (info.parameterTypes.size() != info.parameterNames.size() || info.parameterTypes.size() > 4)
        throw std::runtime_error("Invalid native registry parameter metadata.");
    if (info.returnType != Type::Int && info.returnType != Type::Bool && info.returnType != Type::Void)
        throw std::runtime_error("Fixed registry ABI supports Int/Bool/Void returns.");
    for (auto type : info.parameterTypes)
        if (type != Type::Int && type != Type::Bool)
            throw std::runtime_error("Fixed registry ABI supports Int/Bool parameters.");
    const auto id = stableBuiltinSymbolId(info.symbol);
    registerFunction(info.symbol, function);
    metadata_.emplace(id, std::move(info));
}
const NativeFunctionRegistry::Metadata *NativeFunctionRegistry::metadata(std::uint64_t id) const {
    auto found = metadata_.find(id);
    return found == metadata_.end() ? nullptr : &found->second;
}
std::string X64Backend::targetTriple() const {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    return "x86_64-pc-windows-msvc";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "x86_64-apple-darwin";
#elif defined(__linux__) && defined(__x86_64__)
    return "x86_64-pc-linux-gnu";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64-unknown-native";
#else
    return "unsupported-architecture";
#endif
}
NativeCode X64Backend::compile(const Module &module, const NativeFunctionRegistry &registry) const {
#if !defined(__x86_64__) && !defined(_M_X64)
    (void)module;
    throw std::runtime_error("The bootstrap native backend currently targets x86-64 only.");
#else
    LoweringDiagnostic diagnostic;
    if (!verify(module, diagnostic))
        throw std::runtime_error(diagnostic.message);
    for (const auto &identity : module.imports)
        if (!standardModule(identity) && !registry.hasModule(identity))
            throw std::runtime_error("Missing native module binding: " + identity);
    if (!module.globals.empty())
        throw std::runtime_error("Bootstrap x64 capability: Globals require LLVM or Interpreter.");
    for (const auto &function : module.functions) {
        if (function.returnType == Type::Float || function.returnType == Type::String ||
            function.returnType == Type::List)
            throw std::runtime_error(
                "Bootstrap x64 capability: non-integer return requires LLVM or Interpreter.");
        for (const auto type : function.valueTypes)
            if (type != Type::Int && type != Type::Bool && type != Type::Void)
                throw std::runtime_error(
                    "Bootstrap x64 capability: Float/String/List requires LLVM or Interpreter.");
    }
    NativeCode result;
    Bytes code;
    std::unordered_map<std::string, CompiledFunction> compiled;
    std::vector<Pending> calls;
    for (const Function &function : module.functions) {
        if (compiled.contains(function.name))
            throw std::runtime_error("Duplicate native function: " + function.name);
        compiled.emplace(function.name, emitFunction(code, function, calls, registry));
    }
    for (const Pending &call : calls) {
        const auto target = compiled.find(call.symbol);
        if (target == compiled.end())
            throw std::runtime_error("Unresolved native function or builtin: " + call.symbol);
        patch32(code, call.at,
                static_cast<std::int64_t>(target->second.start) - static_cast<std::int64_t>(call.at + 4));
    }
    // Host-call trampolines adapt the platform C ABI to the internal stack argument convention.
    std::unordered_map<std::string, std::size_t> trampolines;
    for (const auto &[name, function] : compiled) {
        trampolines[name] = code.size();
        for (std::uint32_t i = function.parameterCount; i > 0; --i) {
#if defined(_WIN32)
            switch (i - 1) {
            case 0:
                code.push_back(0x51);
                break;
            case 1:
                code.push_back(0x52);
                break;
            case 2:
                code.insert(code.end(), {0x41, 0x50});
                break;
            case 3:
                code.insert(code.end(), {0x41, 0x51});
                break;
            default:
                throw std::runtime_error("Native entry supports at most four parameters.");
            }
#else
            switch (i - 1) {
            case 0:
                code.push_back(0x57);
                break;
            case 1:
                code.push_back(0x56);
                break;
            case 2:
                code.push_back(0x52);
                break;
            case 3:
                code.push_back(0x51);
                break;
            default:
                throw std::runtime_error("Native entry supports at most four parameters.");
            }
#endif
        }
        code.push_back(0xe8);
        const std::size_t at = code.size();
        u32(code, 0);
        patch32(code, at, static_cast<std::int64_t>(function.start) - static_cast<std::int64_t>(at + 4));
        if (function.parameterCount) {
            code.insert(code.end(), {0x48, 0x81, 0xc4});
            u32(code, function.parameterCount * 8U);
        }
        code.push_back(0xc3);
    }
    for (const auto &function : module.functions)
        result.returnTypes_[function.name] = function.returnType;
    for (const auto &[name, offset] : trampolines) {
        result.offsets_[name] = offset;
        result.arities_[name] = compiled.at(name).parameterCount;
    }
    result.memorySize_ = (code.size() + 4095U) & ~std::size_t(4095U);
    result.memory_ = allocateExecutable(result.memorySize_);
    if (!result.memory_)
        throw std::runtime_error("Could not allocate executable memory for x86-64 code.");
    std::memcpy(result.memory_, code.data(), code.size());
    try {
        protectExecutable(result.memory_, result.memorySize_);
    } catch (...) {
        freeExecutable(result.memory_, result.memorySize_);
        result.memory_ = nullptr;
        result.memorySize_ = 0;
        throw;
    }
    for (const auto &[name, function] : compiled)
        result.code_[name] =
            Bytes(code.begin() + static_cast<std::ptrdiff_t>(function.start),
                  code.begin() + static_cast<std::ptrdiff_t>(function.start + function.size));
    return result;
#endif
}

} // namespace jm::script::ir

namespace jm::script::ir {
void NativeFunctionRegistry::registerTypedFunction(Metadata info, TypedFunction function) {
    if (!function || info.parameterNames.size() != info.parameterTypes.size() ||
        info.parameterTypes.size() > 4)
        throw std::runtime_error("Invalid typed FFI metadata/function.");
    for (auto type : info.parameterTypes)
        if (type != Type::Int && type != Type::Float && type != Type::Bool && type != Type::String)
            throw std::runtime_error("Typed FFI currently accepts Int/Float/Bool/String.");
    if (info.returnType != Type::Int && info.returnType != Type::Float && info.returnType != Type::Bool &&
        info.returnType != Type::String && info.returnType != Type::Void)
        throw std::runtime_error("Typed FFI return type is unsupported.");
    auto id = stableBuiltinSymbolId(info.symbol);
    if (metadata_.contains(id) || functions_.contains(id) || typed_.contains(id))
        throw std::runtime_error("Duplicate native symbol: " + info.symbol);
    metadata_[id] = info;
    typed_[id] = std::make_shared<TypedBinding>(TypedBinding{std::move(info), std::move(function)});
}
const NativeFunctionRegistry::TypedBinding *NativeFunctionRegistry::typed(std::uint64_t id) const {
    auto found = typed_.find(id);
    return found == typed_.end() ? nullptr : found->second.get();
}
std::vector<NativeFunctionRegistry::Metadata> NativeFunctionRegistry::allMetadata() const {
    std::vector<Metadata> result;
    for (const auto &[id, info] : metadata_)
        result.push_back(info);
    return result;
}
} // namespace jm::script::ir
