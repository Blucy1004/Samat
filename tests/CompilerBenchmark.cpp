#include "JMEngine/Script/JMIR.hpp"
#include <chrono>
#include <iostream>

int main() {
    using namespace jm::script;
    using namespace jm::script::ir;
    Program program;
    Diagnostic diagnostic;
    if (!parseCode("fn main():\n    let sum = 0\n    for i in 0..10000:\n        sum += i\n    return sum\n",
                   program, diagnostic)) {
        std::cerr << diagnostic.message;
        return 1;
    }
    Module module;
    LoweringDiagnostic lowering;
    if (!lower(program, module, lowering)) {
        std::cerr << lowering.message;
        return 1;
    }
#ifdef NDEBUG
    std::cout << "Release benchmark; compile time excluded; 10 invocations, 10000 loop steps each.\n";
#else
    std::cout << "Debug benchmark; compile time excluded; 10 invocations, 10000 loop steps each.\n";
#endif
    auto measure = [](const char* backend, auto invoke) {
        if (invoke() != 49995000)
            throw std::runtime_error("Benchmark warmup result mismatch.");
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 10; ++i)
            if (invoke() != 49995000)
                throw std::runtime_error("Benchmark result mismatch.");
        std::cout
            << backend << ": "
            << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()
            << " ms\n";
    };
    try {
        RunOptions options;
        options.entryFunction = "main";
        measure("Interpreter",
                [&] { return std::get<std::int64_t>(execute(program, options).returnValue.data); });
        auto bootstrap = X64Backend{}.compile(module);
        measure("Bootstrap x64", [&] { return bootstrap.invoke("main"); });
        if (LLVMBackend::available()) {
            auto llvm = LLVMBackend{}.compile(module),
                 optimized = LLVMBackend{true}.compile(optimize(module));
            measure("LLVM JIT O0", [&] { return llvm.invoke("main"); });
            measure("LLVM JIT O2", [&] { return optimized.invoke("main"); });
        } else
            std::cout << "CAPABILITY SKIP LLVM: unavailable\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
