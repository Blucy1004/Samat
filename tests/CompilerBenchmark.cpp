#include "Samat/Language/JMIR.hpp"
#include <chrono>
#include <iostream>
#include <vector>

int main() {
    using namespace jm::script;
    using namespace jm::script::ir;
    struct Case {
        std::string name, source, expected;
        int limit;
        bool bootstrap;
    };
    const std::vector<Case> cases{
        {"integer accumulation",
         "fn work(limit: Int) -> Int:\n    let total = 0\n    for i in 0..limit:\n "
         "       total += i\n    return total\n",
         "49995000", 10000, true},
        {"Float accumulation",
         "fn work(limit: Int) -> Float:\n    let total: Float = 0.0\n    for i in "
         "0..limit:\n        total += float(i) * 0.5\n    return total\n",
         "24997500", 10000, false},
        {"collection construction and foreach",
         "fn work(limit: Int) -> Int:\n    let values: List<Int> = []\n    for i "
         "in 0..limit:\n        values.push(i)\n    let total = 0\n    for value "
         "in values:\n        total += value\n    return total\n",
         "499500", 1000, false},
        {"Vector2 accumulation",
         "fn work(limit: Int) -> Float:\n    let position = Vector2(0.0, 0.0)\n    let velocity = "
         "Vector2(1.0, 2.0)\n    for i in 0..limit:\n        position += velocity\n    return position.x + "
         "position.y\n",
         "3000", 1000, false},
        {"Struct field accumulation",
         "struct Counter:\n    total: Int\nfn work(limit: Int) -> Int:\n    let state = Counter(total: 0)\n  "
         "  for i in 0..limit:\n        state.total += i\n    return state.total\n",
         "499500", 1000, false},
        {"Map lookup",
         "fn work(limit: Int) -> Int:\n    let scores: Map<String, Int> = {\"JM\": 42}\n    let total = 0\n  "
         "  for i in 0..limit:\n        total += scores[\"JM\"]\n    return total\n",
         "42000", 1000, false},
        {"String concatenation",
         "fn work(limit: Int) -> Int:\n    let text = \"\"\n    for i in 0..limit:\n        text += \"x\"\n  "
         "  return text.length\n",
         "1000", 1000, false}};
#ifdef NDEBUG
    std::cout << "Release benchmark; ";
#else
    std::cout << "Debug benchmark; ";
#endif
    std::cout << "compile/lazy JIT warmup excluded; 10 invocations; results checked.\n"
                 "Native work takes a runtime limit; the interpreter invokes a main wrapper.\n"
                 "Optimizers may replace integer loops with closed forms. These are microbenchmarks.\n";
    try {
        for (const auto &item : cases) {
            Program program;
            Diagnostic diagnostic;
            auto source = item.source + "fn main():\n    return work(" + std::to_string(item.limit) + ")\n";
            if (!parseCode(source, program, diagnostic))
                throw std::runtime_error(diagnostic.message);
            Module module;
            LoweringDiagnostic lowering;
            if (!lower(program, module, lowering))
                throw std::runtime_error(lowering.message);
            std::cout << item.name << " (" << item.limit << " elements/steps):\n";
            auto measure = [&](const char *name, auto invoke) {
                if (invoke().toString() != item.expected)
                    throw std::runtime_error("Benchmark warmup mismatch.");
                auto start = std::chrono::steady_clock::now();
                for (int i = 0; i < 10; ++i)
                    if (invoke().toString() != item.expected)
                        throw std::runtime_error("Benchmark result mismatch.");
                std::cout << "  " << name << ": "
                          << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                       start)
                                 .count()
                          << " ms\n";
            };
            RunOptions options;
            options.entryFunction = "main";
            measure("Interpreter", [&] { return execute(program, options).returnValue; });
            if (item.bootstrap) {
                auto code = X64Backend{}.compile(module);
                measure("Bootstrap x64", [&] { return code.invokeValue("work", {Value(item.limit)}); });
            } else
                std::cout << "  Bootstrap x64: UNSUPPORTED\n";
            if (LLVMBackend::available())
                for (bool optimized : {false, true}) {
                    auto code = LLVMBackend{optimized}.compile(module);
                    measure(optimized ? "LLVM JIT O2" : "LLVM JIT O0",
                            [&] { return code.invokeValue("work", {Value(item.limit)}); });
                }
            else
                std::cout << "  LLVM JIT: UNTESTED (disabled)\n";
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
