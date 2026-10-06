#include "JMEngine/Script/JMIR.hpp"
#include "JMEngine/Script/RuntimeABI.h"
#include "JMEngine/Script/StandardLibrary.hpp"
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <mutex>
#include <sstream>

#if JMENGINE_HAS_LLVM
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/Transforms/Utils/Cloning.h>
#endif

namespace jm::script::ir {
LLVMBackend::LLVMBackend(bool optimized, std::string target)
    : optimized_(optimized), target_(std::move(target)) {}
bool LLVMBackend::available() { return JMENGINE_HAS_LLVM; }
#if JMENGINE_HAS_LLVM
namespace {
void initialize() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        llvm::InitializeAllTargetInfos();
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmPrinters();
        llvm::InitializeAllAsmParsers();
    });
}
std::string llvmError(llvm::Error error) { return llvm::toString(std::move(error)); }
void verified(const llvm::Module &module) {
    std::string message;
    llvm::raw_string_ostream out(message);
    if (llvm::verifyModule(module, &out))
        throw std::runtime_error("JM4002: LLVM verification failed: " + out.str());
}
llvm::Type *scalar(llvm::LLVMContext &context, Type type) {
    if (type == Type::Float)
        return llvm::Type::getDoubleTy(context);
    if (type == Type::Void)
        return llvm::Type::getVoidTy(context);
    if (type != Type::String && type != Type::List && type != Type::Int && type != Type::Bool &&
        type != Type::Vector2 && type != Type::Vector3 && type != Type::Color && type != Type::Struct &&
        type != Type::Tuple && type != Type::Map && type != Type::Range)
        throw std::runtime_error("LLVM scalar capability excludes " + typeName(type) + ". Use Interpreter.");
    return llvm::Type::getInt64Ty(context);
}
std::unique_ptr<llvm::TargetMachine> machine(const std::string &triple) {
    initialize();
    std::string error;
    auto *target = llvm::TargetRegistry::lookupTarget(triple, error);
    if (!target)
        throw std::runtime_error("LLVM target unavailable: " + error);
    llvm::TargetOptions options;
    auto *result = target->createTargetMachine(triple, "generic", "", options, llvm::Reloc::PIC_);
    if (!result)
        throw std::runtime_error("LLVM could not create target machine.");
    return std::unique_ptr<llvm::TargetMachine>(result);
}
std::string functionName(const std::string &name) { return "__jm_fn_" + name; }
std::string wrapperName(const std::string &name) { return "__jm_invoke_" + name; }
std::uint64_t encodeAggregate(const Value &value) {
    auto type = value.type();
    auto result = jm_runtime_call(JM_RT_AGGREGATE_CREATE, static_cast<int>(type), 0, 0);
    std::vector<double> fields;
    if (type == Type::Vector2) {
        auto v = std::get<Vector2Value>(value.data);
        fields = {v.x, v.y};
    } else if (type == Type::Vector3) {
        auto v = std::get<Vector3Value>(value.data);
        fields = {v.x, v.y, v.z};
    } else {
        auto v = std::get<ColorValue>(value.data);
        fields = {v.r, v.g, v.b, v.a};
    }
    for (auto v : fields)
        jm_runtime_call(JM_RT_AGGREGATE_APPEND, result, std::bit_cast<uint64_t>(v), static_cast<int>(type));
    return result;
}
Value decodeFFI(std::uint64_t bits, Type type) {
    if (type == Type::List) {
        Value::Array result;
        auto element = static_cast<Type>(jm_list_element_type(bits));
        result.elementType = element;
        auto count = jm_runtime_call(JM_RT_LENGTH, bits, 0, JM_RT_LIST);
        for (uint64_t i = 0; i < count; ++i)
            result.push_back(
                decodeFFI(jm_runtime_call(JM_RT_LIST_GET, bits, i, static_cast<int>(element)), element));
        return Value::array(std::move(result));
    }
    if (type == Type::Vector2 || type == Type::Vector3 || type == Type::Color) {
        auto field = [&](uint64_t i) {
            return std::bit_cast<double>(jm_runtime_call(JM_RT_FIELD_GET, bits, i, 0));
        };
        if (type == Type::Vector2)
            return Value(Vector2Value{field(0), field(1)});
        if (type == Type::Vector3)
            return Value(Vector3Value{field(0), field(1), field(2)});
        return Value(ColorValue{field(0), field(1), field(2), field(3)});
    }
    if (type == Type::Float)
        return Value(std::bit_cast<double>(bits));
    if (type == Type::Bool)
        return Value(bits != 0);
    if (type == Type::Int)
        return Value(std::bit_cast<std::int64_t>(bits));
    if (type == Type::String) {
        std::uint64_t length;
        auto *bytes = jm_string_bytes(bits, &length);
        return Value(std::string(bytes, length));
    }
    return Value{};
}
std::uint64_t typedDispatch(std::uint64_t cookie, std::uint64_t a, std::uint64_t b, std::uint64_t c,
                            std::uint64_t d) {
    auto &binding = *reinterpret_cast<const NativeFunctionRegistry::TypedBinding *>(cookie);
    std::uint64_t bits[]{a, b, c, d};
    std::vector<Value> args;
    for (size_t i = 0; i < binding.metadata.parameterTypes.size(); ++i)
        args.push_back(decodeFFI(bits[i], binding.metadata.parameterTypes[i]));
    auto value = binding.function(args);
    auto type = binding.metadata.returnType;
    if (type == Type::Float && value.type() == Type::Int)
        value = Value(static_cast<double>(std::get<std::int64_t>(value.data)));
    if (value.type() != type)
        throw std::runtime_error("JM7001: Typed FFI returned a value that does not match metadata.");
    if (type == Type::List) {
        auto element = binding.metadata.returnElementType;
        auto list = jm_runtime_call(JM_RT_LIST_CREATE, static_cast<int>(element), 0, 0);
        for (auto item : *std::get<Value::ArrayPtr>(value.data)) {
            if (element == Type::Float && item.type() == Type::Int)
                item = Value(static_cast<double>(std::get<int64_t>(item.data)));
            if (item.type() != element)
                throw std::runtime_error("JM7001: List FFI return element mismatch.");
            uint64_t bits = element == Type::Int     ? static_cast<uint64_t>(std::get<int64_t>(item.data))
                            : element == Type::Float ? std::bit_cast<uint64_t>(std::get<double>(item.data))
                            : element == Type::Bool  ? std::get<bool>(item.data)
                                                     : 0;
            if (element == Type::String) {
                auto &text = std::get<std::string>(item.data);
                bits = jm_string_create(text.data(), text.size());
            }
            jm_runtime_call(JM_RT_LIST_PUSH, list, bits, static_cast<int>(element));
        }
        return list;
    }
    if (type == Type::Vector2 || type == Type::Vector3 || type == Type::Color)
        return encodeAggregate(value);
    if (type == Type::Float)
        return std::bit_cast<std::uint64_t>(std::get<double>(value.data));
    if (type == Type::Int)
        return static_cast<std::uint64_t>(std::get<std::int64_t>(value.data));
    if (type == Type::Bool)
        return std::get<bool>(value.data) ? 1 : 0;
    if (type == Type::String) {
        const auto &text = std::get<std::string>(value.data);
        return jm_string_create(text.data(), text.size());
    }
    return 0;
}
[[noreturn]] void arithmeticFault(std::int64_t code) {
    throw std::runtime_error(code == 1   ? "JM3001: Cannot divide by zero."
                             : code == 2 ? "JM3002: Integer division overflow."
                             : code == 4 ? "JM3004: Shift count must be in 0..63."
                             : code == 5 ? "JM3005: Float is outside Int conversion range."
                             : code == 6 ? "JM3005: clamp minimum exceeds maximum."
                                         : "JM3002: Integer abs overflow.");
}
std::unique_ptr<llvm::Module> translate(const Module &input, llvm::LLVMContext &context,
                                        const std::string &triple, bool wrappers, bool entry, bool aot,
                                        const NativeFunctionRegistry *registry = nullptr) {
    LoweringDiagnostic diagnostic;
    if (!verify(input, diagnostic))
        throw std::runtime_error(diagnostic.message);
    auto result = std::make_unique<llvm::Module>("Samat", context);
    result->setTargetTriple(triple);
    auto target = machine(triple);
    result->setDataLayout(target->createDataLayout());
    llvm::IRBuilder<> b(context);
    auto *i64 = b.getInt64Ty();
    if (aot && llvm::Triple(triple).isOSWindows()) {
        // MSVC COFF emits this marker for SSE floating-point code. A weak definition
        // supports freestanding scalar executables and lets a supplied CRT override it.
        auto *marker = new llvm::GlobalVariable(*result, b.getInt32Ty(), false,
                                                llvm::GlobalValue::WeakODRLinkage, b.getInt32(0), "_fltused");
        marker->setComdat(result->getOrInsertComdat("_fltused"));
    }
    std::unordered_map<std::string, llvm::Function *> functions;
    std::unordered_map<std::string, llvm::GlobalVariable *> globals;
    std::unordered_map<std::string, uint64_t> globalRoots;
    for (const auto &global : input.globals) {
        globalRoots[global.name] = globalRoots.size() + 1;
        auto *type = scalar(context, global.type);
        globals[global.name] =
            new llvm::GlobalVariable(*result, type, false, llvm::GlobalValue::InternalLinkage,
                                     llvm::Constant::getNullValue(type), "__jm_global_" + global.name);
    }
    for (const auto &fn : input.functions) {
        std::vector<llvm::Type *> parameters;
        for (auto type : fn.parameterTypes)
            parameters.push_back(scalar(context, type));
        auto *function =
            llvm::Function::Create(llvm::FunctionType::get(scalar(context, fn.returnType), parameters, false),
                                   llvm::GlobalValue::ExternalLinkage, functionName(fn.name), *result);
        function->setUWTableKind(llvm::UWTableKind::Default);
        functions[fn.name] = function;
    }
    llvm::Function *fault = nullptr;
    auto faultFunction = [&]() {
        if (fault)
            return fault;
        fault = llvm::Function::Create(llvm::FunctionType::get(b.getVoidTy(), {i64}, false),
                                       aot ? llvm::GlobalValue::InternalLinkage
                                           : llvm::GlobalValue::ExternalLinkage,
                                       "__jm_arithmetic_fault", *result);
        fault->addFnAttr(llvm::Attribute::NoReturn);
        if (aot) {
            auto saved = b.saveIP();
            b.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", fault));
            if (llvm::Triple(triple).isOSWindows()) {
                auto *trap = llvm::Intrinsic::getDeclaration(result.get(), llvm::Intrinsic::trap);
                b.CreateCall(trap);
            } else {
                auto puts = result->getOrInsertFunction(
                    "puts", llvm::FunctionType::get(b.getInt32Ty(), {b.getPtrTy()}, false));
                auto exit = result->getOrInsertFunction(
                    "exit", llvm::FunctionType::get(b.getVoidTy(), {b.getInt32Ty()}, false));
                b.CreateCall(
                    puts,
                    {b.CreateGlobalStringPtr(
                        "Samat runtime error: arithmetic fault (zero divisor or integer overflow).")});
                b.CreateCall(exit, {b.getInt32(1)});
            }
            b.CreateUnreachable();
            b.restoreIP(saved);
        }
        return fault;
    };
    for (const auto &fn : input.functions) {
        auto *function = functions.at(fn.name);
        std::vector<llvm::BasicBlock *> blocks;
        for (const auto &block : fn.blocks)
            blocks.push_back(llvm::BasicBlock::Create(context, block.name, function));
        b.SetInsertPoint(blocks[0]);
        std::vector<llvm::AllocaInst *> locals, values;
        for (std::size_t i = 0; i < fn.localCount; ++i) {
            auto *type = scalar(context, fn.localTypes[i]);
            auto *slot = b.CreateAlloca(type, nullptr, "local" + std::to_string(i));
            locals.push_back(slot);
            b.CreateStore(llvm::Constant::getNullValue(type), slot);
        }
        for (std::size_t i = 0; i < fn.valueCount; ++i) {
            auto *type = fn.valueTypes[i] == Type::Void ? i64 : scalar(context, fn.valueTypes[i]);
            values.push_back(b.CreateAlloca(type, nullptr, "value" + std::to_string(i)));
        }
        std::size_t index = 0;
        for (auto &argument : function->args())
            b.CreateStore(&argument, locals.at(index++));
        auto load = [&](ValueId id) {
            return b.CreateLoad(values.at(id)->getAllocatedType(), values.at(id));
        };
        auto truth = [&](llvm::Value *value) -> llvm::Value * {
            if (value->getType()->isDoubleTy())
                return b.CreateFCmpUNE(value, llvm::ConstantFP::get(value->getType(), 0.0));
            return b.CreateICmpNE(value, b.getInt64(0));
        };
        for (const auto &block : fn.blocks) {
            b.SetInsertPoint(blocks[block.id]);
            for (const auto &in : block.instructions) {
                llvm::Value *value = nullptr;
                switch (in.op) {
                case Op::Constant:
                    value = b.getInt64(in.immediate);
                    break;
                case Op::StringConstant: {
                    auto callee = result->getOrInsertFunction(
                        "jm_string_create", llvm::FunctionType::get(i64, {b.getPtrTy(), i64}, false));
                    value = b.CreateCall(
                        callee, {b.CreateGlobalStringPtr(llvm::StringRef(in.symbol.data(), in.symbol.size())),
                                 b.getInt64(in.symbol.size())});
                    break;
                }
                case Op::RuntimeCall: {
                    std::vector<llvm::Value *> args{b.getInt64(in.immediate)};
                    for (auto id : in.arguments) {
                        auto *arg = load(id);
                        args.push_back(fn.valueTypes[id] == Type::Float ? b.CreateBitCast(arg, i64) : arg);
                    }
                    while (args.size() < 4)
                        args.push_back(b.getInt64(0));
                    auto callee = result->getOrInsertFunction(
                        "jm_runtime_call", llvm::FunctionType::get(i64, {i64, i64, i64, i64}, false));
                    auto *raw = b.CreateCall(callee, args);
                    value = in.type == Type::Float ? b.CreateBitCast(raw, b.getDoubleTy()) : raw;
                    break;
                }
                case Op::FloatConstant:
                    value = llvm::ConstantFP::get(b.getDoubleTy(), in.floating);
                    break;
                case Op::Load:
                    value = b.CreateLoad(locals.at(in.local)->getAllocatedType(), locals.at(in.local));
                    break;
                case Op::Store:
                    b.CreateStore(load(in.left), locals.at(in.local));
                    break;
                case Op::GlobalLoad:
                    value = b.CreateLoad(globals.at(in.symbol)->getValueType(), globals.at(in.symbol));
                    break;
                case Op::GlobalStore:
                    b.CreateStore(load(in.left), globals.at(in.symbol));
                    if (in.type == Type::String || in.type == Type::List || in.type == Type::Vector2 ||
                        in.type == Type::Vector3 || in.type == Type::Color || in.type == Type::Struct ||
                        in.type == Type::Tuple) {
                        auto callee = result->getOrInsertFunction(
                            "jm_runtime_root", llvm::FunctionType::get(b.getVoidTy(), {i64, i64}, false));
                        b.CreateCall(callee, {b.getInt64(globalRoots.at(in.symbol)), load(in.left)});
                    }
                    break;
                case Op::FloatToInt: {
                    auto *input = load(in.left);
                    auto *bad = llvm::BasicBlock::Create(context, "conversion.error", function);
                    auto *good = llvm::BasicBlock::Create(context, "conversion.ok", function);
                    auto *low = llvm::ConstantFP::get(b.getDoubleTy(), -9223372036854775808.0);
                    auto *high = llvm::ConstantFP::get(b.getDoubleTy(), 9223372036854775808.0);
                    b.CreateCondBr(b.CreateAnd(b.CreateFCmpOGE(input, low), b.CreateFCmpOLT(input, high)),
                                   good, bad);
                    b.SetInsertPoint(bad);
                    b.CreateCall(faultFunction(), {b.getInt64(5)});
                    b.CreateUnreachable();
                    b.SetInsertPoint(good);
                    value = b.CreateFPToSI(input, i64);
                    break;
                }
                case Op::BitNot:
                    value = b.CreateNot(load(in.left));
                    break;
                case Op::IntToFloat:
                    value = b.CreateSIToFP(load(in.left), b.getDoubleTy());
                    break;
                case Op::Negate:
                    value = fn.valueTypes[in.left] == Type::Float ? b.CreateFNeg(load(in.left))
                                                                  : b.CreateNeg(load(in.left));
                    break;
                case Op::LogicalNot:
                    value = b.CreateZExt(b.CreateNot(truth(load(in.left))), i64);
                    break;
                case Op::ToBoolean:
                    value = b.CreateZExt(truth(load(in.left)), i64);
                    break;
                case Op::Call: {
                    std::vector<llvm::Value *> arguments;
                    for (auto id : in.arguments)
                        arguments.push_back(load(id));
                    if (auto found = functions.find(in.symbol); found != functions.end()) {
                        auto *call = b.CreateCall(found->second, arguments);
                        value = in.type == Type::Void ? static_cast<llvm::Value *>(b.getInt64(0)) : call;
                    } else if (auto standard = standardFunction(in.symbol)) {
                        const auto operation = in.symbol.substr(13);
                        const bool floating = in.type == Type::Float;
                        auto compare = [&](llvm::Value *left, llvm::Value *right) {
                            return floating ? b.CreateFCmpOLT(left, right) : b.CreateICmpSLT(left, right);
                        };
                        if (operation == "min" || operation == "max") {
                            value = arguments[0];
                            for (std::size_t i = 1; i < arguments.size(); ++i) {
                                auto *smaller = operation == "min" ? compare(arguments[i], value)
                                                                   : compare(value, arguments[i]);
                                value = b.CreateSelect(smaller, arguments[i], value);
                            }
                        } else if (operation == "clamp") {
                            auto *bad = llvm::BasicBlock::Create(context, "clamp.error", function);
                            auto *good = llvm::BasicBlock::Create(context, "clamp.ok", function);
                            b.CreateCondBr(compare(arguments[2], arguments[1]), bad, good);
                            b.SetInsertPoint(bad);
                            b.CreateCall(faultFunction(), {b.getInt64(6)});
                            b.CreateUnreachable();
                            b.SetInsertPoint(good);
                            value = b.CreateSelect(compare(arguments[0], arguments[1]), arguments[1],
                                                   arguments[0]);
                            value = b.CreateSelect(compare(arguments[2], value), arguments[2], value);
                        } else if (operation == "abs" && !floating) {
                            auto *bad = llvm::BasicBlock::Create(context, "abs.overflow", function);
                            auto *good = llvm::BasicBlock::Create(context, "abs.ok", function);
                            b.CreateCondBr(
                                b.CreateICmpEQ(arguments[0],
                                               b.getInt64(std::numeric_limits<std::int64_t>::min())),
                                bad, good);
                            b.SetInsertPoint(bad);
                            b.CreateCall(faultFunction(), {b.getInt64(3)});
                            b.CreateUnreachable();
                            b.SetInsertPoint(good);
                            value = b.CreateSelect(b.CreateICmpSLT(arguments[0], b.getInt64(0)),
                                                   b.CreateNeg(arguments[0]), arguments[0]);
                        } else {
                            auto id = operation == "sqrt"    ? llvm::Intrinsic::sqrt
                                      : operation == "sin"   ? llvm::Intrinsic::sin
                                      : operation == "cos"   ? llvm::Intrinsic::cos
                                      : operation == "floor" ? llvm::Intrinsic::floor
                                      : operation == "ceil"  ? llvm::Intrinsic::ceil
                                      : operation == "round" ? llvm::Intrinsic::round
                                      : operation == "pow"   ? llvm::Intrinsic::pow
                                                             : llvm::Intrinsic::fabs;
                            value = b.CreateCall(
                                llvm::Intrinsic::getDeclaration(result.get(), id, {b.getDoubleTy()}),
                                arguments);
                        }
                    } else if (registry && registry->typed(in.symbolId)) {
                        auto *binding = registry->typed(in.symbolId);
                        std::vector<llvm::Value *> raw{b.getInt64(reinterpret_cast<std::uintptr_t>(binding))};
                        for (size_t i = 0; i < arguments.size(); ++i)
                            raw.push_back(fn.valueTypes[in.arguments[i]] == Type::Float
                                              ? b.CreateBitCast(arguments[i], i64)
                                              : arguments[i]);
                        while (raw.size() < 5)
                            raw.push_back(b.getInt64(0));
                        auto callee = result->getOrInsertFunction(
                            "jm_typed_dispatch",
                            llvm::FunctionType::get(i64, {i64, i64, i64, i64, i64}, false));
                        auto *resultBits = b.CreateCall(callee, raw);
                        value = in.type == Type::Float ? b.CreateBitCast(resultBits, b.getDoubleTy())
                                                       : resultBits;
                    } else {
                        for (auto *argument : arguments)
                            if (!argument->getType()->isIntegerTy(64))
                                throw std::runtime_error(
                                    "Fixed NativeFunctionRegistry ABI requires Int/Bool arguments: " +
                                    in.symbol);
                        if (arguments.size() > 4)
                            throw std::runtime_error("Native registry accepts at most four arguments.");
                        while (arguments.size() < 4)
                            arguments.push_back(b.getInt64(0));
                        auto callee = result->getOrInsertFunction(
                            in.symbol, llvm::FunctionType::get(i64, {i64, i64, i64, i64}, false));
                        value = b.CreateCall(callee, arguments);
                    }
                    break;
                }
                default: {
                    auto *left = load(in.left);
                    auto *right = load(in.right);
                    const bool floating = left->getType()->isDoubleTy();
                    switch (in.op) {
                    case Op::BitAnd:
                        value = b.CreateAnd(left, right);
                        break;
                    case Op::BitOr:
                        value = b.CreateOr(left, right);
                        break;
                    case Op::BitXor:
                        value = b.CreateXor(left, right);
                        break;
                    case Op::ShiftLeft:
                    case Op::ShiftRight: {
                        auto *bad = llvm::BasicBlock::Create(context, "shift.error", function);
                        auto *good = llvm::BasicBlock::Create(context, "shift.ok", function);
                        b.CreateCondBr(b.CreateICmpULT(right, b.getInt64(64)), good, bad);
                        b.SetInsertPoint(bad);
                        b.CreateCall(faultFunction(), {b.getInt64(4)});
                        b.CreateUnreachable();
                        b.SetInsertPoint(good);
                        value = in.op == Op::ShiftLeft ? b.CreateShl(left, right) : b.CreateAShr(left, right);
                        break;
                    }
                    case Op::Add:
                        value = floating ? b.CreateFAdd(left, right) : b.CreateAdd(left, right);
                        break;
                    case Op::Subtract:
                        value = floating ? b.CreateFSub(left, right) : b.CreateSub(left, right);
                        break;
                    case Op::Multiply:
                        value = floating ? b.CreateFMul(left, right) : b.CreateMul(left, right);
                        break;
                    case Op::Divide:
                    case Op::Modulo: {
                        auto *bad = llvm::BasicBlock::Create(context, "arithmetic.error", function);
                        auto *good = llvm::BasicBlock::Create(context, "arithmetic.ok", function);
                        llvm::Value *zero =
                            floating ? b.CreateFCmpOEQ(right, llvm::ConstantFP::get(b.getDoubleTy(), 0))
                                     : b.CreateICmpEQ(right, b.getInt64(0));
                        llvm::Value *invalid = zero;
                        if (!floating)
                            invalid = b.CreateOr(
                                zero,
                                b.CreateAnd(b.CreateICmpEQ(
                                                left, b.getInt64(std::numeric_limits<std::int64_t>::min())),
                                            b.CreateICmpEQ(right, b.getInt64(-1))));
                        b.CreateCondBr(invalid, bad, good);
                        b.SetInsertPoint(bad);
                        b.CreateCall(faultFunction(), {b.CreateSelect(zero, b.getInt64(1), b.getInt64(2))});
                        b.CreateUnreachable();
                        b.SetInsertPoint(good);
                        value = floating ? (in.op == Op::Divide ? b.CreateFDiv(left, right)
                                                                : b.CreateFRem(left, right))
                                         : (in.op == Op::Divide ? b.CreateSDiv(left, right)
                                                                : b.CreateSRem(left, right));
                        break;
                    }
                    case Op::Equal:
                        value = floating ? b.CreateFCmpOEQ(left, right) : b.CreateICmpEQ(left, right);
                        break;
                    case Op::NotEqual:
                        value = floating ? b.CreateFCmpUNE(left, right) : b.CreateICmpNE(left, right);
                        break;
                    case Op::Less:
                        value = floating ? b.CreateFCmpOLT(left, right) : b.CreateICmpSLT(left, right);
                        break;
                    case Op::LessEqual:
                        value = floating ? b.CreateFCmpOLE(left, right) : b.CreateICmpSLE(left, right);
                        break;
                    case Op::Greater:
                        value = floating ? b.CreateFCmpOGT(left, right) : b.CreateICmpSGT(left, right);
                        break;
                    case Op::GreaterEqual:
                        value = floating ? b.CreateFCmpOGE(left, right) : b.CreateICmpSGE(left, right);
                        break;
                    case Op::BooleanAnd:
                        value = b.CreateAnd(truth(left), truth(right));
                        break;
                    case Op::BooleanOr:
                        value = b.CreateOr(truth(left), truth(right));
                        break;
                    default:
                        throw std::runtime_error("Unsupported LLVM instruction.");
                    }
                    if (value->getType()->isIntegerTy(1))
                        value = b.CreateZExt(value, i64);
                    break;
                }
                }
                if (value)
                    b.CreateStore(value, values.at(in.result));
            }
            const auto &term = block.terminator;
            if (term.kind == Terminator::Kind::Return) {
                if (fn.returnType == Type::Void)
                    b.CreateRetVoid();
                else
                    b.CreateRet(load(term.value));
            } else if (term.kind == Terminator::Kind::Branch)
                b.CreateBr(blocks.at(term.first));
            else
                b.CreateCondBr(truth(load(term.value)), blocks.at(term.first), blocks.at(term.second));
        }
    }
    if (wrappers)
        for (const auto &fn : input.functions) {
            if (fn.parameterCount > 4)
                throw std::runtime_error("LLVM JIT invocation supports at most four scalar parameters.");
            auto *wrapper =
                llvm::Function::Create(llvm::FunctionType::get(i64, {i64, i64, i64, i64}, false),
                                       llvm::GlobalValue::ExternalLinkage, wrapperName(fn.name), *result);
            wrapper->setUWTableKind(llvm::UWTableKind::Default);
            b.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", wrapper));
            std::vector<llvm::Value *> arguments;
            std::size_t index = 0;
            for (auto &argument : wrapper->args()) {
                if (index >= fn.parameterCount)
                    break;
                arguments.push_back(fn.parameterTypes[index++] == Type::Float
                                        ? b.CreateBitCast(&argument, b.getDoubleTy())
                                        : &argument);
            }
            auto *call = b.CreateCall(functions.at(fn.name), arguments);
            if (fn.returnType == Type::Void)
                b.CreateRet(b.getInt64(0));
            else
                b.CreateRet(fn.returnType == Type::Float ? b.CreateBitCast(call, i64) : call);
        }
    if (entry) {
        if (!functions.contains("main"))
            throw std::runtime_error("AOT requires a main() function.");
        const auto &fn = *std::find_if(input.functions.begin(), input.functions.end(),
                                       [](const auto &f) { return f.name == "main"; });
        if (fn.parameterCount ||
            (fn.returnType != Type::Int && fn.returnType != Type::Bool && fn.returnType != Type::Void))
            throw std::runtime_error("AOT entry main must have no parameters and return Int/Bool/Void.");
        const bool windows = llvm::Triple(triple).isOSWindows();
        auto *wrapper = llvm::Function::Create(llvm::FunctionType::get(b.getInt32Ty(), {}, false),
                                               llvm::GlobalValue::ExternalLinkage,
                                               windows ? "jm_entry" : "main", *result);
        b.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", wrapper));
        if (aot && (result->getFunction("jm_runtime_call") || result->getFunction("jm_string_create"))) {
            auto abi = result->getOrInsertFunction("jm_runtime_require_abi",
                                                   llvm::FunctionType::get(b.getVoidTy(), {i64}, false));
            b.CreateCall(abi, {b.getInt64(JM_RUNTIME_ABI_VERSION)});
            auto callee = result->getOrInsertFunction("jm_runtime_set_aot",
                                                      llvm::FunctionType::get(b.getVoidTy(), {i64}, false));
            b.CreateCall(callee, {b.getInt64(1)});
        }
        if (functions.contains("__jm_init"))
            b.CreateCall(functions.at("__jm_init"));
        auto *call = b.CreateCall(functions.at("main"));
        b.CreateRet(fn.returnType == Type::Void ? b.getInt32(0) : b.CreateTrunc(call, b.getInt32Ty()));
    }
    auto *imports = result->getOrInsertNamedMetadata("jm.imports");
    for (const auto &identity : input.imports)
        imports->addOperand(llvm::MDNode::get(context, llvm::MDString::get(context, identity)));
    if (!input.events.empty()) {
        auto *recordType = llvm::StructType::get(context, {b.getPtrTy(), b.getPtrTy()});
        std::vector<llvm::Constant *> records;
        for (const auto &event : input.events) {
            auto *text = b.CreateGlobalString(event.event, "__jm_event_name", 0, result.get());
            records.push_back(llvm::ConstantStruct::get(recordType, {text, functions.at(event.function)}));
        }
        auto *tableType = llvm::ArrayType::get(recordType, records.size());
        new llvm::GlobalVariable(*result, tableType, true, llvm::GlobalValue::ExternalLinkage,
                                 llvm::ConstantArray::get(tableType, records), "__jm_event_table");
        new llvm::GlobalVariable(*result, i64, true, llvm::GlobalValue::ExternalLinkage,
                                 b.getInt64(records.size()), "__jm_event_count");
    }
    verified(*result);
    return result;
}
void optimizeLLVM(llvm::Module &module, bool enabled) {
    if (!enabled)
        return;
    llvm::LoopAnalysisManager loops;
    llvm::FunctionAnalysisManager functions;
    llvm::CGSCCAnalysisManager cgscc;
    llvm::ModuleAnalysisManager modules;
    llvm::PassBuilder builder;
    builder.registerModuleAnalyses(modules);
    builder.registerCGSCCAnalyses(cgscc);
    builder.registerFunctionAnalyses(functions);
    builder.registerLoopAnalyses(loops);
    builder.crossRegisterProxies(loops, functions, cgscc, modules);
    auto passes = builder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
    passes.run(module, modules);
    verified(module);
}
std::string shellQuote(const std::string &value) {
#ifdef _WIN32
    if (value.find_first_of("\"\r\n%") != std::string::npos)
        throw std::runtime_error("Unsupported character in linker path.");
    return "\"" + value + "\"";
#else
    std::string result = "'";
    for (char ch : value)
        result += ch == '\'' ? "'\\''" : std::string(1, ch);
    return result + "'";
#endif
}
} // namespace
std::string LLVMBackend::targetTriple() const {
    return target_.empty() ? llvm::sys::getDefaultTargetTriple() : target_;
}
NativeCode LLVMBackend::compile(const Module &input, const NativeFunctionRegistry &registry) const {
    jm_runtime_require_abi(JM_RUNTIME_ABI_VERSION);
    initialize();
    for (const auto &identity : input.imports)
        if (!standardModule(identity) && !registry.hasModule(identity))
            throw std::runtime_error("Missing native module binding: " + identity);
    if (llvm::Triple(targetTriple()).getArch() !=
            llvm::Triple(llvm::sys::getDefaultTargetTriple()).getArch() ||
        llvm::Triple(targetTriple()).getOS() != llvm::Triple(llvm::sys::getDefaultTargetTriple()).getOS())
        throw std::runtime_error(
            "LLVM JIT requires the host target. Use emitObject/build for cross compilation.");
    auto created = llvm::orc::LLJITBuilder().create();
    if (!created)
        throw std::runtime_error(llvmError(created.takeError()));
    auto jit = std::shared_ptr<llvm::orc::LLJIT>(std::move(*created));
    auto process = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
        jit->getDataLayout().getGlobalPrefix());
    if (!process)
        throw std::runtime_error(llvmError(process.takeError()));
    jit->getMainJITDylib().addGenerator(std::move(*process));
    auto bindings = std::make_shared<NativeFunctionRegistry>(registry);
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = translate(input, *context, targetTriple(), true, false, false, bindings.get());
    optimizeLLVM(*module, optimized_);
    llvm::orc::SymbolMap symbols;
    symbols[jit->mangleAndIntern("__jm_arithmetic_fault")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&arithmeticFault), llvm::JITSymbolFlags::Exported);
    for (const auto &fn : input.functions)
        for (const auto &block : fn.blocks)
            for (const auto &in : block.instructions)
                if (in.op == Op::Call && in.symbolId) {
                    if (std::any_of(input.functions.begin(), input.functions.end(),
                                    [&](const auto &fn) { return fn.name == in.symbol; }) ||
                        standardFunction(in.symbol))
                        continue;
                    auto callback = registry.find(in.symbolId);
                    if (!callback && !registry.typed(in.symbolId))
                        throw std::runtime_error("Missing native binding: " + in.symbol);
                    const auto *info = registry.metadata(in.symbolId);
                    if (info && info->parameterTypes.size() != in.arguments.size())
                        throw std::runtime_error("Native metadata argument count mismatch: " + in.symbol);
                    if (info) {
                        if (info->returnType != in.type)
                            throw std::runtime_error("Native registry return type does not match JM IR: " +
                                                     in.symbol);
                        for (std::size_t i = 0; i < in.arguments.size(); ++i)
                            if (fn.valueTypes.at(in.arguments[i]) != info->parameterTypes[i])
                                throw std::runtime_error("Native registry parameter type mismatch: " +
                                                         in.symbol);
                    }
                    for (std::size_t i = 0; i < in.argumentNames.size(); ++i)
                        if (!in.argumentNames[i].empty() &&
                            (!info || in.argumentNames[i] != info->parameterNames[i]))
                            throw std::runtime_error(
                                "Native named arguments require matching registry metadata order: " +
                                in.symbol);
                    if (callback)
                        symbols[jit->mangleAndIntern(in.symbol)] = llvm::orc::ExecutorSymbolDef(
                            llvm::orc::ExecutorAddr::fromPtr(callback), llvm::JITSymbolFlags::Exported);
                }
    symbols[jit->mangleAndIntern("jm_typed_dispatch")] = {llvm::orc::ExecutorAddr::fromPtr(&typedDispatch),
                                                          llvm::JITSymbolFlags::Exported};
    symbols[jit->mangleAndIntern("jm_runtime_root")] = {llvm::orc::ExecutorAddr::fromPtr(&jm_runtime_root),
                                                        llvm::JITSymbolFlags::Exported};
    symbols[jit->mangleAndIntern("jm_runtime_call")] = {llvm::orc::ExecutorAddr::fromPtr(&jm_runtime_call),
                                                        llvm::JITSymbolFlags::Exported};
    symbols[jit->mangleAndIntern("jm_string_create")] = {llvm::orc::ExecutorAddr::fromPtr(&jm_string_create),
                                                         llvm::JITSymbolFlags::Exported};
    if (auto error = jit->getMainJITDylib().define(llvm::orc::absoluteSymbols(std::move(symbols))))
        throw std::runtime_error(llvmError(std::move(error)));
    if (auto error = jit->addIRModule(llvm::orc::ThreadSafeModule(std::move(module), std::move(context))))
        throw std::runtime_error(llvmError(std::move(error)));
    struct Signature {
        Type result;
        std::vector<Type> parameters, elements;
    };
    std::unordered_map<std::string, Signature> signatures;
    for (const auto &fn : input.functions)
        signatures[fn.name] = {fn.returnType, fn.parameterTypes, fn.parameterElementTypes};
    struct RuntimeOwner {
        std::recursive_mutex mutex;
        bool running{};
        void *context = jm_runtime_create_context();
        ~RuntimeOwner() { jm_runtime_destroy_context(context); }
    };
    auto owner = std::make_shared<RuntimeOwner>();
    NativeCode result;
    result.invoker_ = [jit, signatures, owner, bindings](const std::string &name,
                                                         const std::vector<Value> &args) -> Value {
        std::lock_guard guard(owner->mutex);
        if (owner->running)
            throw std::runtime_error(
                "JM7002: Reentrant host invocation of the same native module is unsupported.");
        struct Running {
            bool &value;
            explicit Running(bool &state) : value(state) { value = true; }
            ~Running() { value = false; }
        } running(owner->running);
        struct Scope {
            void *previous;
            explicit Scope(void *context) : previous(jm_runtime_activate(context)) {}
            ~Scope() {
                jm_runtime_collect();
                jm_runtime_activate(previous);
            }
        } scope(owner->context);
        auto found = signatures.find(name);
        if (found == signatures.end())
            throw std::runtime_error("Native function not found: " + name);
        const auto &signature = found->second;
        if (signature.result == Type::Struct || signature.result == Type::Tuple ||
            signature.result == Type::Map || signature.result == Type::Range)
            throw std::runtime_error(
                "JM6002: Public native record returns require layout metadata; unsupported.");
        if (args.size() != signature.parameters.size())
            throw std::runtime_error("LLVM invocation argument count mismatch: " + name);
        std::uint64_t bits[4]{};
        for (std::size_t i = 0; i < args.size(); ++i) {
            if ((signature.parameters[i] == Type::Vector2 || signature.parameters[i] == Type::Vector3 ||
                 signature.parameters[i] == Type::Color) &&
                args[i].type() == signature.parameters[i])
                bits[i] = encodeAggregate(args[i]);
            else if (signature.parameters[i] == Type::Float) {
                double number;
                if (args[i].type() == Type::Float)
                    number = std::get<double>(args[i].data);
                else if (args[i].type() == Type::Int)
                    number = static_cast<double>(std::get<std::int64_t>(args[i].data));
                else
                    throw std::runtime_error("Float argument required.");
                bits[i] = std::bit_cast<std::uint64_t>(number);
            } else if (signature.parameters[i] == Type::List && args[i].type() == Type::List) {
                auto element = i < signature.elements.size() ? signature.elements[i] : Type::Any;
                if (element == Type::Any)
                    throw std::runtime_error("Native List invocation needs List<T> metadata.");
                auto handle = jm_runtime_call(JM_RT_LIST_CREATE, static_cast<int>(element), 0, 0);
                for (auto value : *std::get<Value::ArrayPtr>(args[i].data)) {
                    if (element == Type::Float && value.type() == Type::Int)
                        value = Value(static_cast<double>(std::get<std::int64_t>(value.data)));
                    if (value.type() != element)
                        throw std::runtime_error("Native List invocation element mismatch.");
                    std::uint64_t bits =
                        element == Type::Int ? static_cast<std::uint64_t>(std::get<std::int64_t>(value.data))
                        : element == Type::Float ? std::bit_cast<std::uint64_t>(std::get<double>(value.data))
                        : element == Type::Bool  ? std::get<bool>(value.data)
                                                 : 0;
                    if (element == Type::String) {
                        auto &text = std::get<std::string>(value.data);
                        bits = jm_string_create(text.data(), text.size());
                    }
                    jm_runtime_call(JM_RT_LIST_PUSH, handle, bits, static_cast<int>(element));
                }
                bits[i] = handle;
            } else if (signature.parameters[i] == Type::String && args[i].type() == Type::String) {
                const auto &text = std::get<std::string>(args[i].data);
                bits[i] = jm_string_create(text.data(), text.size());
            } else if (signature.parameters[i] == Type::Int && args[i].type() == Type::Int)
                bits[i] = static_cast<std::uint64_t>(std::get<std::int64_t>(args[i].data));
            else if (signature.parameters[i] == Type::Bool && args[i].type() == Type::Bool)
                bits[i] = std::get<bool>(args[i].data) ? 1 : 0;
            else
                throw std::runtime_error("Int/Bool argument required.");
        }
        auto symbol = jit->lookup(wrapperName(name));
        if (!symbol)
            throw std::runtime_error(llvmError(symbol.takeError()));
        using Entry = std::uint64_t (*)(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
        const auto output = symbol->toPtr<Entry>()(bits[0], bits[1], bits[2], bits[3]);
        if (signature.result == Type::Vector2 || signature.result == Type::Vector3 ||
            signature.result == Type::Color)
            return decodeFFI(output, signature.result);
        if (signature.result == Type::String) {
            std::uint64_t size;
            auto *bytes = jm_string_bytes(output, &size);
            return Value(std::string(bytes, size));
        }
        if (signature.result == Type::List) {
            Value::Array result;
            auto element = static_cast<Type>(jm_list_element_type(output));
            result.elementType = element;
            auto size = jm_runtime_call(JM_RT_LENGTH, output, 0, JM_RT_LIST);
            for (std::uint64_t i = 0; i < size; ++i)
                result.push_back(decodeFFI(
                    jm_runtime_call(JM_RT_LIST_GET, output, i, static_cast<int>(element)), element));
            return Value::array(std::move(result));
        }
        if (signature.result == Type::Float)
            return Value(std::bit_cast<double>(output));
        if (signature.result == Type::Bool)
            return Value(output != 0);
        if (signature.result == Type::Void)
            return Value{};
        return Value(std::bit_cast<std::int64_t>(output));
    };
    if (signatures.contains("__jm_init"))
        result.invokeValue("__jm_init");
    return result;
}
std::string LLVMBackend::emitIR(const Module &input) const {
    llvm::LLVMContext context;
    auto module = translate(input, context, targetTriple(), false, false, false);
    optimizeLLVM(*module, optimized_);
    std::string text;
    llvm::raw_string_ostream out(text);
    module->print(out, nullptr);
    return out.str();
}
void LLVMBackend::emitObject(const Module &input, const std::string &path, bool entryWrapper) const {
    llvm::LLVMContext context;
    auto module = translate(input, context, targetTriple(), false, entryWrapper, true);
    optimizeLLVM(*module, optimized_);
    auto target = machine(targetTriple());
    std::error_code error;
    llvm::raw_fd_ostream output(path, error, llvm::sys::fs::OF_None);
    if (error)
        throw std::runtime_error("Cannot write object file: " + error.message());
    llvm::legacy::PassManager passes;
    if (target->addPassesToEmitFile(passes, output, nullptr, llvm::CodeGenFileType::ObjectFile))
        throw std::runtime_error("LLVM target cannot emit object files.");
    passes.run(*module);
    output.flush();
    if (output.has_error())
        throw std::runtime_error("Object file write failed.");
}
void LLVMBackend::build(const Module &input, const std::string &output, const std::string &linker) const {
    for (const auto &identity : input.imports)
        if (!standardModule(identity))
            throw std::runtime_error("AOT module requires an external runtime/link integration: " + identity +
                                     ". Object emission remains available.");
    bool runtime = false;
    for (const auto &fn : input.functions)
        for (const auto &block : fn.blocks)
            for (const auto &in : block.instructions)
                runtime |= in.op == Op::StringConstant || in.op == Op::RuntimeCall;
    if (runtime && llvm::Triple(targetTriple()).isOSWindows())
        throw std::runtime_error("JM6003: Windows String/List AOT needs a Windows-built runtime archive; "
                                 "COFF emission remains available.");
    std::filesystem::path archive;
    if (runtime) {
#if defined(__linux__)
        archive = std::filesystem::read_symlink("/proc/self/exe").parent_path() / "libSamat_runtime.a";
#else
        throw std::runtime_error("JM6003: Runtime AOT archive discovery currently requires Linux.");
#endif
        if (!std::filesystem::exists(archive))
            throw std::runtime_error(
                "JM6003: Place libSamat_runtime.a beside the compiler for String/List AOT.");
    }
    auto object = output + ".jm.o";
    if (std::filesystem::exists(object))
        throw std::runtime_error("Refusing to overwrite temporary object: " + object);
    emitObject(input, object, true);
    const bool windows = llvm::Triple(targetTriple()).isOSWindows();
    std::string command =
        shellQuote(linker.empty() ? (windows ? JMENGINE_LLD_LINK : JMENGINE_CLANG) : linker);
    if (windows)
        command += " /entry:jm_entry /subsystem:console /nodefaultlib " + shellQuote("/out:" + output) + " " +
                   shellQuote(object);
    else
        command += " -o " + shellQuote(output) + " " + shellQuote(object) + " -lm";
    if (runtime)
        command += " " + shellQuote(archive.string()) + " -lstdc++ -lm";
    const int status = std::system(command.c_str());
    std::filesystem::remove(object);
    if (status != 0)
        throw std::runtime_error(
            "AOT linker failed. See linker diagnostics for missing target runtime symbols/libraries; verify "
            "clang/lld-link availability or pass --linker <path>.");
}
#else
namespace {
[[noreturn]] void unavailable() {
    throw std::runtime_error("JM5001: LLVM backend is unavailable. Configure with LLVM development packages "
                             "and JMENGINE_ENABLE_LLVM=ON; Interpreter and bootstrap x64 remain available.");
}
} // namespace
std::string LLVMBackend::targetTriple() const { return target_.empty() ? "unavailable" : target_; }
NativeCode LLVMBackend::compile(const Module &, const NativeFunctionRegistry &) const { unavailable(); }
std::string LLVMBackend::emitIR(const Module &) const { unavailable(); }
void LLVMBackend::emitObject(const Module &, const std::string &, bool) const { unavailable(); }
void LLVMBackend::build(const Module &, const std::string &, const std::string &) const { unavailable(); }
#endif
} // namespace jm::script::ir
