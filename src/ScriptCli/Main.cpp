#include "JMEngine/Script/JMIR.hpp"
#include "JMEngine/Script/LanguageCore.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
void help() {
    std::cout << "Samat / 訓C正音 v0.5\n"
                 "  SamatCompiler [run] file.st [--entry main]\n"
                 "  SamatCompiler check file.st\n"
                 "  SamatCompiler --ast|--ir|--code|--korean file.st\n"
                 "  SamatCompiler --native function file.st [i64 args...]\n"
                 "  SamatCompiler --llvm-jit function file.st [scalar args...] [-O]\n"
                 "  SamatCompiler --emit-llvm file.st [-O] [--target triple]\n"
                 "  SamatCompiler --emit-obj output.o file.st [--target triple]\n"
                 "  SamatCompiler build file.st -o program [--target triple] [--linker path]\n"
                 "  SamatCompiler --build output file.st\n"
                 "  SamatCompiler --help|--version\n";
}
void showDiagnostic(const jm::script::Diagnostic& diagnostic) {
    std::cerr << diagnostic.code;
    if (diagnostic.line)
        std::cerr << " at " << diagnostic.line << ':' << diagnostic.column;
    std::cerr << ": " << diagnostic.message << '\n';
    if (!diagnostic.suggestion.empty())
        std::cerr << "  " << diagnostic.suggestion << '\n';
}
void ast(const jm::script::StatementList& statements, std::size_t depth = 0) {
    static const char* kinds[]{"Variable", "Assignment", "Expression", "If",     "While",
                               "ForRange", "ForEach",    "Function",   "Return", "Break",
                               "Continue", "Import",     "Event"};
    for (const auto& item : statements) {
        std::cout << std::string(depth * 2, ' ') << kinds[static_cast<unsigned>(item.kind)];
        if (!item.name.empty())
            std::cout << ' ' << item.name;
        if (item.declaredType != jm::script::Type::Any)
            std::cout << " : " << jm::script::typeName(item.declaredType);
        if (item.kind == jm::script::Statement::Kind::Function)
            std::cout << " (" << item.parameters.size() << " parameters) -> "
                      << jm::script::typeName(item.returnType);
        if (item.expression) {
            jm::script::Program expression;
            jm::script::Statement statement;
            statement.expression = item.expression;
            expression.statements.push_back(statement);
            auto text = jm::script::renderCode(expression);
            std::cout << " = " << text.substr(0, text.size() - 1);
        }
        std::cout << '\n';
        ast(item.body, depth + 1);
        if (!item.alternative.empty()) {
            std::cout << std::string((depth + 1) * 2, ' ') << "Else\n";
            ast(item.alternative, depth + 2);
        }
    }
}
} // namespace
int main(int argc, char** argv) {
    using namespace jm::script;
    if (argc < 2) {
        help();
        return 2;
    }
    try {
        std::string mode = "run", file, function, output, target, linker, entry;
        bool optimized = false;
        std::vector<std::string> positional;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help();
                return 0;
            }
            if (arg == "--version") {
                std::cout << "Samat / 訓C正音 0.5.0 (LLVM "
                          << (ir::LLVMBackend::available() ? "enabled" : "unavailable") << ")\n";
                return 0;
            }
            if (arg == "-O" || arg == "-O2") {
                optimized = true;
                continue;
            }
            if (arg == "-o" || arg == "--target" || arg == "--linker" || arg == "--entry") {
                if (++i >= argc)
                    throw std::runtime_error("Missing value for " + arg);
                (arg == "-o"         ? output
                 : arg == "--target" ? target
                 : arg == "--linker" ? linker
                                     : entry) = argv[i];
                continue;
            }
            if (i == 1 &&
                (arg == "run" || arg == "check" || arg == "build" || arg == "--ir" || arg == "--ast" ||
                 arg == "--native" || arg == "--llvm-jit" || arg == "--emit-llvm" || arg == "--emit-obj" ||
                 arg == "--build" || arg == "--code" || arg == "--korean")) {
                mode = arg;
                continue;
            }
            positional.push_back(arg);
        }
        if (mode == "--native" || mode == "--llvm-jit") {
            if (positional.size() < 2)
                throw std::runtime_error("Native mode requires a function name and input file.");
            function = positional[0];
            file = positional[1];
        } else if (mode == "--emit-obj" || mode == "--build") {
            if (positional.size() != 2)
                throw std::runtime_error("Emission requires output path and input file.");
            output = positional[0];
            file = positional[1];
        } else {
            if (positional.size() != 1)
                throw std::runtime_error("Expected one input file. See --help.");
            file = positional[0];
        }
        std::ifstream input(file, std::ios::binary);
        if (!input)
            throw std::runtime_error("Could not open script: " + file);
        const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        Program program;
        Diagnostic codeDiagnostic, koreanDiagnostic;
        if (!parseCode(source, program, codeDiagnostic) && !parseKorean(source, program, koreanDiagnostic)) {
            bool korean = source.find("함수 ") != std::string::npos ||
                          source.find("변수 ") != std::string::npos ||
                          source.find("정한다") != std::string::npos;
            showDiagnostic(korean ? koreanDiagnostic : codeDiagnostic);
            return 1;
        }
        std::vector<Diagnostic> diagnostics;
        if (!check(program, diagnostics)) {
            for (const auto& diagnostic : diagnostics)
                showDiagnostic(diagnostic);
            return 1;
        }
        if (mode == "check") {
            std::cout << "Check passed.\n";
            return 0;
        }
        if (mode == "--ast") {
            ast(program.statements);
            return 0;
        }
        if (mode == "--code" || mode == "--korean") {
            std::cout << (mode == "--code" ? renderCode(program) : renderKorean(program));
            return 0;
        }
        if (mode == "run") {
            RunOptions options;
            options.entryFunction = entry;
            options.eventName = "start";
            bool main = false, actions = false;
            for (const auto& statement : program.statements) {
                main |= statement.kind == Statement::Kind::Function && statement.name == "main";
                actions |= statement.kind != Statement::Kind::Function &&
                           statement.kind != Statement::Kind::Variable &&
                           statement.kind != Statement::Kind::Import &&
                           statement.kind != Statement::Kind::Event;
            }
            if (entry.empty() && main && !actions)
                options.entryFunction = "main";
            const auto result = execute(program, options);
            for (const auto& line : result.output)
                std::cout << line << '\n';
            if (!options.entryFunction.empty())
                std::cout << "result: " << result.returnValue.toString() << '\n';
            return 0;
        }
        ir::Module module;
        ir::LoweringDiagnostic lowering;
        if (!ir::lower(program, module, lowering))
            throw std::runtime_error(lowering.message);
        if (optimized)
            module = ir::optimize(module);
        if (mode == "--ir") {
            std::cout << ir::format(module);
            return 0;
        }
        ir::LLVMBackend llvm(optimized, target);
        if (mode == "--emit-llvm") {
            std::cout << llvm.emitIR(module);
            return 0;
        }
        if (mode == "--emit-obj") {
            llvm.emitObject(module, output);
            std::cout << "Object: " << output << " (" << llvm.targetTriple() << ")\n";
            return 0;
        }
        if (mode == "build" || mode == "--build") {
            if (output.empty())
                throw std::runtime_error("build requires -o <output>.");
            llvm.build(module, output, linker);
            std::cout << "AOT executable: " << output << " (" << llvm.targetTriple() << ")\n";
            return 0;
        }
        if (mode == "--native" || mode == "--llvm-jit") {
            std::vector<Value> arguments;
            for (std::size_t i = 2; i < positional.size(); ++i) {
                auto expression = parseExpression(positional[i]);
                if (!expression || (expression->kind != Expression::Kind::Literal &&
                                    expression->kind != Expression::Kind::Unary))
                    throw std::runtime_error("Scalar literal native argument required.");
                Environment empty;
                auto value = evaluateExpression(expression, empty);
                if (mode == "--native" && value.type() != Type::Int)
                    throw std::runtime_error("Bootstrap arguments must be i64.");
                arguments.push_back(value);
            }
            ir::X64Backend bootstrap;
            auto native = mode == "--native" ? bootstrap.compile(module) : llvm.compile(module);
            auto result = native.invokeValue(function, arguments);
            std::cout << "target: " << (mode == "--native" ? bootstrap.targetTriple() : llvm.targetTriple())
                      << "\nresult: " << result.toString() << '\n';
            if (mode == "--native") {
                const auto& bytes = native.machineCode(function);
                std::cout << "machine code (" << bytes.size() << " bytes):\n";
                for (std::size_t i = 0; i < bytes.size(); ++i) {
                    if (i % 16 == 0)
                        std::cout << '\n';
                    std::cout << std::hex << std::setw(2) << std::setfill('0')
                              << static_cast<unsigned>(bytes[i]) << ' ';
                }
                std::cout << std::dec << '\n';
            }
            return 0;
        }
        throw std::runtime_error("Unknown mode. See --help.");
    } catch (const std::exception& error) {
        std::cerr << "Samat error: " << error.what() << '\n';
        return 1;
    }
}
