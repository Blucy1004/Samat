#if JMENGINE_HAS_ENGINE
#include "JMEngine/Script/EngineScriptAPI.hpp"
#endif
#include "JMEngine/Script/JMIR.hpp"
#include "JMEngine/Script/LanguageCore.hpp"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace {
using namespace jm::script;
using namespace jm::script::ir;
std::size_t passed{}, skipped{};
void require(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
Program parse(const std::string &source) {
    Program program;
    Diagnostic diagnostic;
    require(parseCode(source, program, diagnostic), "Parse failed: " + diagnostic.message + "\n" + source);
    return program;
}
Module lowerProgram(const Program &program) {
    Module module;
    LoweringDiagnostic diagnostic;
    require(lower(program, module, diagnostic), "Lowering failed: " + diagnostic.message);
    return module;
}
Value interpret(const Program &program) {
    RunOptions options;
    options.entryFunction = "main";
    return execute(program, options).returnValue;
}
void roundTrip(const Program &program) {
    Program korean, code;
    Diagnostic diagnostic;
    const auto koreanText = renderKorean(program);
    require(parseKorean(koreanText, korean, diagnostic),
            "Korean round trip failed: " + diagnostic.message + "\n" + koreanText);
    require(structurallyEqual(program, korean), "Code/Korean shared AST mismatch.\n" + koreanText);
    require(parseCode(renderCode(korean), code, diagnostic), "Code renderer parse failed.");
    require(structurallyEqual(korean, code), "Korean/Code shared AST mismatch.");
}
void fault(const std::string &source, const std::string &fragment) {
    bool failed = false;
    try {
        (void)interpret(parse(source));
    } catch (const std::exception &error) {
        failed = std::string(error.what()).find(fragment) != std::string::npos;
    }
    require(failed, "Expected diagnostic containing: " + fragment);
    ++passed;
}
void scalar(const std::string &name, const std::string &source, const std::string &expected,
            bool bootstrap = true) {
    auto program = parse(source);
    require(interpret(program).toString() == expected, name + ": Interpreter result differs.");
    roundTrip(program);
    auto module = lowerProgram(program);
    auto optimized = optimize(module);
    if (bootstrap) {
        auto native = X64Backend{}.compile(module), fast = X64Backend{}.compile(optimized);
        require(native.invokeValue("main").toString() == expected &&
                    fast.invokeValue("main").toString() == expected,
                name + ": Bootstrap/optimizer differs.");
    } else {
        ++skipped;
        std::cout << "CAPABILITY SKIP bootstrap: " << name << '\n';
    }
    if (LLVMBackend::available()) {
        auto native = LLVMBackend{}.compile(module), fast = LLVMBackend{true}.compile(optimized);
        require(native.invokeValue("main").toString() == expected &&
                    fast.invokeValue("main").toString() == expected,
                name + ": LLVM O0/O2 differs.");
    } else {
        ++skipped;
        std::cout << "CAPABILITY SKIP LLVM: " << name << '\n';
    }
    ++passed;
    std::cout << "PASS " << name << '\n';
}
void interpreterOnly(const std::string &name, const std::string &source, const std::string &expected) {
    auto program = parse(source);
    roundTrip(program);
    require(interpret(program).toString() == expected, name + ": result differs.");
    Module module;
    LoweringDiagnostic diagnostic;
    require(!lower(program, module, diagnostic), name + ": unsupported native feature was accepted.");
    ++passed;
    ++skipped;
    std::cout << "PASS " << name << "; CAPABILITY SKIP scalar native: " << diagnostic.message << '\n';
}
void aot() {
    if (!LLVMBackend::available()) {
        ++skipped;
        std::cout << "CAPABILITY SKIP AOT: LLVM unavailable\n";
        return;
    }
    auto directory =
        std::filesystem::temp_directory_path() /
        ("jm-aot-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    try {
        auto program = parse(
            "fn factorial(n: Int) -> Int:\n    if n <= 1:\n        return 1\n    return n * factorial(n - "
            "1)\nfn main() -> Int:\n    if factorial(10) == 3628800:\n        return 42\n    return 1\n");
        auto module = lowerProgram(program);
        auto executable = directory / "factorial";
#ifdef _WIN32
        executable += ".exe";
#endif
        LLVMBackend{true}.build(module, executable.string());
        require(std::filesystem::file_size(executable) > 0, "AOT file missing.");
        auto status = std::system(('"' + executable.string() + '"').c_str());
#ifdef _WIN32
        require(status == 42, "AOT factorial check failed.");
#else
        require(WIFEXITED(status) && WEXITSTATUS(status) == 42, "AOT factorial check failed.");
#endif
        LLVMBackend{false, "x86_64-pc-windows-msvc"}.emitObject(module,
                                                                (directory / "factorial.obj").string(), true);
        require(std::filesystem::file_size(directory / "factorial.obj") > 0, "Windows COFF object missing.");
        ++passed;
        std::cout << "PASS standalone AOT factorial result check (exit 42), Windows COFF emission\n";
    } catch (...) {
        std::filesystem::remove_all(directory);
        throw;
    }
    std::filesystem::remove_all(directory);
}
void verifier() {
    auto original = lowerProgram(parse("fn main():\n    let x = 1\n    return x + 2\n"));
    LoweringDiagnostic diagnostic;
    auto missing = original;
    missing.functions[0].blocks[0].terminator.kind = Terminator::Kind::None;
    require(!verify(missing, diagnostic), "Missing terminator accepted.");
    auto branch = original;
    branch.functions[0].blocks[0].terminator = {Terminator::Kind::Branch, 0, 999, 0};
    require(!verify(branch, diagnostic), "Invalid target accepted.");
    auto local = original;
    for (auto &in : local.functions[0].blocks[0].instructions)
        if (in.op == Op::Load)
            in.local = 999;
    require(!verify(local, diagnostic), "Invalid local accepted.");
    auto operand = original;
    operand.functions[0].blocks[0].terminator.value = 999;
    require(!verify(operand, diagnostic), "Undefined operand accepted.");
    auto type = original;
    type.functions[0].returnType = Type::Float;
    require(!verify(type, diagnostic), "Return mismatch accepted.");
    auto call = lowerProgram(parse("fn add(a):\n    return a\nfn main():\n    return add(1)\n"));
    for (auto &in : call.functions[1].blocks[0].instructions)
        if (in.op == Op::Call)
            in.arguments.clear();
    require(!verify(call, diagnostic), "Call arity mismatch accepted.");
    ++passed;
}
void callbacks() {
    auto program = parse("fn main():\n    return player.jump(force: 12)\n");
    auto module = lowerProgram(program);
    NativeFunctionRegistry registry;
    registry.registerFunction(
        {"builtin.player.jump",
         "player.jump",
         "플레이어.점프",
         "Test callback",
         {"force"},
         {Type::Int},
         Type::Int},
        [](std::int64_t a, std::int64_t, std::int64_t, std::int64_t) { return a + 100; });
    HostFunction host = [](const std::string &name, const std::vector<Value> &args,
                           const std::vector<std::string> &) -> Value {
        if (name == "builtin.player.jump")
            return Value(std::get<std::int64_t>(args.at(0).data) + 100);
        throw std::out_of_range("Unbound");
    };
    RunOptions options;
    options.entryFunction = "main";
    require(execute(program, options, host).returnValue.toString() == "112", "Interpreter FFI mismatch.");
    require(X64Backend{}.compile(module, registry).invoke("main") == 112, "Bootstrap callback failed.");
    if (LLVMBackend::available())
        require(LLVMBackend{}.compile(module, registry).invoke("main") == 112, "LLVM callback failed.");
    ++passed;
}
void nativeErrors() {
    for (const auto &source : {"fn bad():\n    return 1 / 0\nfn main():\n    return bad() + 42\n",
                               "fn main():\n    return -9223372036854775808 / -1\n"}) {
        const auto module = lowerProgram(parse(source));
        auto checkError = [&](const auto &native) {
            bool failed = false;
            try {
                native.invokeValue("main");
            } catch (const std::exception &error) {
                failed = std::string(error.what()).find("JM300") != std::string::npos;
            }
            require(failed, "Native arithmetic fault must return a diagnostic.");
        };
        checkError(X64Backend{}.compile(module));
        if (LLVMBackend::available()) {
            checkError(LLVMBackend{}.compile(module));
            checkError(LLVMBackend{true}.compile(optimize(module)));
        }
        ++passed;
    }
    auto wrong = parse("fn main():\n    return player.move(speed: 8, direction: 1)\n");
    auto module = lowerProgram(wrong);
    NativeFunctionRegistry registry;
    registry.registerFunction(
        {"builtin.player.move",
         "player.move",
         "플레이어.이동",
         "Metadata order test",
         {"direction", "speed"},
         {Type::Int, Type::Int},
         Type::Int},
        [](std::int64_t, std::int64_t, std::int64_t, std::int64_t) { return std::int64_t{0}; });
    bool rejected = false;
    try {
        X64Backend{}.compile(module, registry);
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "Named argument order must not silently change semantics.");
    ++passed;
}
void engineEvents() {
#if JMENGINE_HAS_ENGINE
    const std::string source =
        "import jm.game\nfn jumpPlayer():\n    if player.grounded:\n        player.jump(force: 12)\n    "
        "return 0\non key.right.held:\n    player.move(direction: 1, speed: 8)\non key.space.pressed:\n    "
        "jumpPlayer()\n";
    auto program = parse(source);
    roundTrip(program);
    auto module = lowerProgram(program);
    auto registry = jm::engineNativeFunctions();
    auto simulate = [&](int backend) {
        jm::Scene scene;
        auto &player = scene.create(jm::ObjectKind::Sprite2D);
        player.position.x = 0;
        player.grounded = true;
        const auto id = player.id;
        jm::EngineScriptContext context{scene, id, {}, 0.25};
        context.input.rightHeld = true;
        auto host = jm::engineHostFunctions(context);
        jm::EngineScriptScope scope(context);
        if (backend == 0) {
            RunOptions options;
            options.moduleResolver = jm::engineModuleResolver();
            options.eventName = "key.right.held";
            execute(program, options, host);
            options.eventName = "key.space.pressed";
            execute(program, options, host);
        } else {
            auto code = backend == 1 ? X64Backend{}.compile(module, registry)
                                     : LLVMBackend{}.compile(module, registry);
            jm::executeNativeEvent(module, code, "key.right.held");
            jm::executeNativeEvent(module, code, "key.space.pressed");
        }
        auto *result = context.player();
        require(result && std::abs(result->position.x - 2.0F) < 0.0001F && result->verticalVelocity == 12 &&
                    !result->grounded,
                "Real engine movement/jump event regression failed.");
        return std::pair{result->position.x, result->verticalVelocity};
    };
    const auto interpreter = simulate(0);
    require(simulate(1) == interpreter, "Engine bootstrap differs.");
    if (LLVMBackend::available())
        require(simulate(2) == interpreter, "Engine LLVM differs.");
    ++passed;
    std::cout << "PASS real Scene movement/jump: shared Code/Korean event AST, Interpreter/bootstrap"
              << (LLVMBackend::available() ? "/LLVM" : "; LLVM capability skip") << '\n';
#else
    ++skipped;
    std::cout << "CAPABILITY SKIP engine events: standalone language build\n";
#endif
}
void differential() {
    std::mt19937 random(504);
    std::string source;
    std::vector<std::pair<std::string, std::int64_t>> expected;
    for (int i = 0; i < 64; ++i) {
        const auto a = static_cast<std::int64_t>(random() % 100),
                   b = static_cast<std::int64_t>(random() % 50 + 1),
                   c = static_cast<std::int64_t>(random() % 10);
        const auto name = "random" + std::to_string(i);
        source +=
            "fn " + name + "():\n    let x = (" + std::to_string(a) + " + " + std::to_string(b) + ") * " +
            std::to_string(c) +
            "\n    let i = 0\n    while i < 3:\n        if x > 100:\n            x -= " + std::to_string(b) +
            "\n        else:\n            x += " + std::to_string(a) + "\n        i += 1\n    return x\n";
        auto value = (a + b) * c;
        for (int step = 0; step < 3; ++step)
            value = value > 100 ? value - b : value + a;
        expected.emplace_back(name, value);
    }
    auto program = parse(source);
    auto module = lowerProgram(program);
    auto optimized = optimize(module);
    auto bootstrap = X64Backend{}.compile(module), bootstrapO = X64Backend{}.compile(optimized);
    NativeCode llvm, llvmO;
    if (LLVMBackend::available()) {
        llvm = LLVMBackend{}.compile(module);
        llvmO = LLVMBackend{true}.compile(optimized);
    }
    for (const auto &[name, expectedValue] : expected) {
        RunOptions options;
        options.entryFunction = name;
        auto result = execute(program, options).returnValue.toString();
        require(result == std::to_string(expectedValue) && bootstrap.invoke(name) == expectedValue &&
                    bootstrapO.invoke(name) == expectedValue,
                "Seeded differential mismatch: " + name);
        if (LLVMBackend::available())
            require(llvm.invoke(name) == expectedValue && llvmO.invoke(name) == expectedValue,
                    "LLVM seeded differential mismatch: " + name);
    }
    passed += expected.size();
    std::cout << "PASS 64 seeded bounded programs: Interpreter/bootstrap"
              << (LLVMBackend::available() ? "/LLVM" : "; LLVM capability skip") << ", optimization off/on\n";
}
} // namespace
int main() {
    try {
        scalar("return42", "fn main():\n    return 42\n", "42");
        scalar("Unicode identifiers",
               "fn 합계(이름을: Int) -> Int:\n    let 점수를: Int = 이름을\n    점수를 += 2\n    return "
               "점수를\nfn main():\n    return 합계(40)\n",
               "42");
        scalar("arithmetic", "fn main():\n    return (10 + 20) * 3\n", "90");
        scalar("comparison", "fn main() -> Bool:\n    return 10 < 20\n", "true");
        scalar("short circuit",
               "fn main():\n    if false and 1 / 0 == 0:\n        return 0\n    if true or 1 / 0 == 0:\n     "
               "   return 42\n    return 1\n",
               "42");
        scalar("inferred float return", "fn main():\n    let x = 3.5\n    return x * 3.0\n", "10.5", false);
        scalar("bool numeric equality", "fn main() -> Bool:\n    return true == 1\n", "false");
        scalar("typed default local", "fn main():\n    let x: Int\n    x += 42\n    return x\n", "42");
        scalar("local mutation", "fn main():\n    let x = 1\n    x += 41\n    return x\n", "42");
        scalar("else if",
               "fn main():\n    if false:\n        return 1\n    else if true:\n        return 42\n    "
               "else:\n        return 0\n",
               "42");
        scalar("nested branches",
               "fn main():\n    if true:\n        if true:\n            return 42\n    return 0\n", "42");
        scalar("break continue",
               "fn main():\n    let x = 0\n    let sum = 0\n    while x < 20:\n        x += 1\n        if x "
               "== 2:\n            continue\n        if x == 5:\n            break\n        sum += x\n    "
               "return sum\n",
               "8");
        scalar("for range",
               "fn main():\n    let sum = 0\n    for i in 0..10:\n        if i == 3:\n            continue\n "
               "       if i == 7:\n            break\n        sum += i\n    return sum\n",
               "18");
        scalar("typed nested calls",
               "fn add(a: Int, b: Int) -> Int:\n    return a + b\nfn main() -> Int:\n    return add(add(10, "
               "20), 12)\n",
               "42");
        scalar("factorial10",
               "fn factorial(n: Int) -> Int:\n    if n <= 1:\n        return 1\n    return n * factorial(n - "
               "1)\nfn main():\n    return factorial(10)\n",
               "3628800");
        scalar("fibonacci10",
               "fn fib(n):\n    if n <= 1:\n        return n\n    return fib(n - 1) + fib(n - 2)\nfn "
               "main():\n    return fib(10)\n",
               "55");
        scalar("gcd",
               "fn gcd(a,b):\n    if b != 0:\n        return gcd(b,a % b)\n    return a\nfn main():\n    "
               "return gcd(48,18)\n",
               "6");
        scalar("mutual recursion",
               "fn even(n):\n    if n == 0:\n        return 1\n    return odd(n - 1)\nfn odd(n):\n    if n "
               "== 0:\n        return 0\n    return even(n - 1)\nfn main():\n    return even(10)\n",
               "1");
        scalar("global mutation",
               "let score: Int = 0\nfn add():\n    score += 1\n    return score\nfn main():\n    add()\n    "
               "add()\n    return score\n",
               "2", false);
        scalar("float boundary",
               "fn scale(x: Float) -> Float:\n    return x * 2.5\nfn main() -> Float:\n    let x: Float = "
               "4\n    return scale(x) + 0.5\n",
               "10.5", false);
        scalar("float stdlib",
               "fn main() -> Float:\n    return pow(2.0, 5.0) + floor(2.8) + ceil(2.1) + round(1.5) + "
               "abs(-3.0)\n",
               "42", false);
        scalar("integer stdlib", "fn main():\n    return abs(-12) + min(30, 18) + max(8, 12)\n", "42", false);
        scalar("exact i64", "fn main():\n    return 9007199254740993 + 2\n", "9007199254740995");
        scalar("minimum i64", "fn main():\n    return -9223372036854775808\n", "-9223372036854775808");
        scalar("wrapping i64", "fn main():\n    return 9223372036854775807 + 1\n", "-9223372036854775808");
        scalar("string concatenation",
               "fn main() -> String:\n    let name: String = \"JM\"\n    return name + \" Engine\"\n",
               "JM Engine", false);
        scalar(
            "list read write push pop",
            "fn main():\n    let values: List = [1, 2, 3]\n    values[1] = 10\n    values.push(4)\n    let "
            "last = values.pop()\n    return values[1] + last + values.length\n",
            "17", false);
        scalar("stdlib module",
               "import jm.math\nfn main() -> Float:\n    return sqrt(81) + clamp(20, 0, 5)\n", "14", false);
        fault("fn bad(x: Void):\n    return 0\nfn main():\n    return 0\n", "JM2003");
        fault("fn main():\n    let x: Int = \"wrong\"\n    return 0\n", "JM2001");
        fault("fn main() -> Int:\n    return \"wrong\"\n", "JM2002");
        fault("fn main():\n    let x = [1]\n    return x[2]\n", "outside");
        fault("fn main():\n    let x = [1]\n    x[-1] = 2\n    return 0\n", "negative");
        fault("fn main():\n    let x = [1]\n    return x[0.5]\n", "JM2005");
        fault("fn main():\n    let x = []\n    x.push(x)\n    return 0\n", "JM3010");
        fault("fn main():\n    let x = []\n    return x.pop()\n", "JM3004");
        fault("fn main():\n    break\n    return 0\n", "JM2009");
        fault("fn main():\n    return 1 / 0\n", "JM3001");
        auto infinite = parse("fn main():\n    while true:\n        continue\n");
        RunOptions bounded;
        bounded.entryFunction = "main";
        bounded.instructionBudget = 50;
        bool stopped = false;
        try {
            execute(infinite, bounded);
        } catch (const std::exception &e) {
            stopped = std::string(e.what()).find("instruction budget") != std::string::npos;
        }
        require(stopped, "Infinite loop budget failed.");
        ++passed;
        bounded.shouldStop = [] { return true; };
        stopped = false;
        try {
            execute(infinite, bounded);
        } catch (const std::exception &e) {
            stopped = std::string(e.what()).find("stopped") != std::string::npos;
        }
        require(stopped, "Cancellation failed.");
        ++passed;
        Program malformed;
        Diagnostic diagnostic;
        require(!parseCode("fn main():\n    return (1 + )\n", malformed, diagnostic) && diagnostic.line == 2,
                "Malformed source line diagnostic missing.");
        ++passed;
        scalar("bitwise operators", "fn main():\n    return bitXor((~0 & 63), 21) | 0\n", "42");
        scalar("checked shifts", "fn main():\n    return (5 << 3) + (-8 >> 2) + 4\n", "42");
        scalar("numeric conversions", "fn main():\n    return int(float(40) + 2.9)\n", "42", false);
        scalar("right associative exponent", "fn main() -> Float:\n    return 2.0 ** 3.0 ** 2.0\n", "512",
               false);
        scalar("modulo assignment", "fn main():\n    let value = 142\n    value %= 100\n    return value\n",
               "42");
        fault("fn main():\n    return 1 << 64\n", "JM3004");
        fault("fn main():\n    return int(pow(2.0, 63.0))\n", "JM3005");
        verifier();
        callbacks();
        nativeErrors();
        engineEvents();
        differential();
        aot();
        std::cout << "Compiler regression: " << passed << " checks passed; " << skipped
                  << " capability skips.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL after " << passed << " checks: " << error.what() << '\n';
        return 1;
    }
}
