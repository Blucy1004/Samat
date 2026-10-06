#include "JMEngine/Script/JMIR.hpp"
#include "JMEngine/Script/LanguageCore.hpp"
#include "JMEngine/Script/ModuleLoader.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace {
jm::script::HostFunction ioHost() {
    using namespace jm::script;
    return [](const std::string &name, const std::vector<Value> &args,
              const std::vector<std::string> &) -> Value {
        if (name == "readLine" || name == "builtin.console.readLine") {
            if (!args.empty())
                throw std::runtime_error("readLine takes no arguments.");
            std::string line;
            std::getline(std::cin, line);
            return Value(line);
        }
        if (name != "builtin.file.exists" && name != "builtin.file.readText" &&
            name != "builtin.file.writeText")
            throw std::out_of_range("Unknown IO capability");
        if (args.size() != (name == "builtin.file.writeText" ? 2u : 1u) || args[0].type() != Type::String)
            throw std::runtime_error("File IO argument mismatch.");
        auto path = std::get<std::string>(args[0].data);
        if (name == "builtin.file.exists")
            return Value(std::filesystem::exists(path));
        if (name == "builtin.file.readText") {
            std::ifstream input(path, std::ios::binary);
            if (!input)
                throw std::runtime_error("Cannot read file: " + path);
            std::string text(std::istreambuf_iterator<char>(input), {});
            if (text.size() > 16 * 1024 * 1024)
                throw std::runtime_error("File read exceeds 16 MiB limit.");
            return Value(std::move(text));
        }
        if (args[1].type() != Type::String)
            throw std::runtime_error("writeText needs String contents.");
        std::ofstream output(path, std::ios::binary);
        auto &text = std::get<std::string>(args[1].data);
        output.write(text.data(), text.size());
        if (!output)
            throw std::runtime_error("Cannot write file: " + path);
        return Value{};
    };
}
int repl() {
    using namespace jm::script;
    auto session = std::make_unique<ExecutionSession>(Program{}, RunOptions{}, ioHost());
    std::string line, source;
    bool block = false;
    while (std::getline(std::cin, line)) {
        if (source.empty() && line == ":quit")
            break;
        if (source.empty() && line == ":reset") {
            session = std::make_unique<ExecutionSession>(Program{}, RunOptions{}, ioHost());
            continue;
        }
        if (source.empty() && line == ":help") {
            std::cout << "Code statements/expressions; blank line submits a block; :reset / :quit\n";
            continue;
        }
        if (!line.empty()) {
            source += line + "\n";
            if (line.ends_with(':'))
                block = true;
            if (block)
                continue;
        }
        if (source.empty())
            continue;
        try {
            auto result = session->evaluate(source);
            for (const auto &output : result.output)
                std::cout << output << '\n';
            if (!result.returnValue.isNull())
                std::cout << result.returnValue.toString() << '\n';
        } catch (const std::exception &error) {
            std::cerr << error.what() << '\n';
        }
        source.clear();
        block = false;
    }
    return 0;
}
void help() {
    std::cout << "Samat / 訓C正音 v0.6\n"
                 "  SamatCompiler [run] file.st [--entry main]\n"
                 "  SamatCompiler check file.st\n"
                 "  SamatCompiler --ast|--ir|--code|--korean file.st\n"
                 "  SamatCompiler --native function file.st [i64 args...]\n"
                 "  SamatCompiler --llvm-jit function file.st [scalar args...] [-O]\n"
                 "  SamatCompiler --emit-llvm file.st [-O] [--target triple]\n"
                 "  SamatCompiler --emit-obj output.o file.st [--target triple]\n"
                 "  SamatCompiler build file.st -o program [--target triple] [--linker path]\n"
                 "  SamatCompiler --build output file.st\n"
                 "  SamatCompiler format file.st | --repl\n  SamatCompiler --help|--version\n";
}
void showDiagnostic(const jm::script::Diagnostic &diagnostic) {
    std::cerr << diagnostic.code;
    if (diagnostic.line)
        std::cerr << " at " << diagnostic.line << ':' << diagnostic.column;
    std::cerr << ": " << diagnostic.message << '\n';
    if (!diagnostic.suggestion.empty())
        std::cerr << "  " << diagnostic.suggestion << '\n';
}
void ast(const jm::script::StatementList &statements, std::size_t depth = 0) {
    static const char *kinds[]{"Variable", "Assignment", "Expression", "If",     "While",
                               "ForRange", "ForEach",    "Function",   "Return", "Break",
                               "Continue", "Import",     "Event",      "Enum",   "Struct"};
    for (const auto &item : statements) {
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
int main(int argc, char **argv) {
    using namespace jm::script;
    if (argc < 2) {
        help();
        return 2;
    }
    try {
        if (argc == 2 && std::string(argv[1]) == "--repl")
            return repl();
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
                std::cout << "Samat / 訓C正音 0.6.0 (LLVM "
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
                (arg == "format" || arg == "run" || arg == "check" || arg == "build" || arg == "--ir" ||
                 arg == "--ast" || arg == "--native" || arg == "--llvm-jit" || arg == "--emit-llvm" ||
                 arg == "--emit-obj" || arg == "--build" || arg == "--code" || arg == "--korean")) {
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
        if (mode != "format" && mode != "--code" && mode != "--korean") {
            auto resolver = [](std::string_view importer,
                               std::string_view request) -> std::optional<ModuleSource> {
                auto path = std::filesystem::weakly_canonical(std::filesystem::path(importer).parent_path() /
                                                              request);
                std::ifstream input(path, std::ios::binary);
                if (!input)
                    return std::nullopt;
                return ModuleSource{path.string(), std::string(std::istreambuf_iterator<char>(input), {})};
            };
            Diagnostic diagnostic;
            Program resolved;
            if (!loadModules({std::filesystem::weakly_canonical(file).string(), source}, resolver, resolved,
                             diagnostic)) {
                showDiagnostic(diagnostic);
                return 1;
            }
            program = std::move(resolved);
        }
        std::vector<Diagnostic> diagnostics;
        if (!check(program, diagnostics)) {
            for (const auto &diagnostic : diagnostics)
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
        if (mode == "format" || mode == "--code" || mode == "--korean") {
            std::cout << (mode != "--korean" ? renderCode(program) : renderKorean(program));
            return 0;
        }
        if (mode == "run") {
            RunOptions options;
            options.entryFunction = entry;
            options.eventName = "start";
            bool main = false, actions = false;
            for (const auto &statement : program.statements) {
                main |= statement.kind == Statement::Kind::Function && statement.name == "main";
                actions |=
                    statement.kind != Statement::Kind::Function &&
                    statement.kind != Statement::Kind::Variable &&
                    statement.kind != Statement::Kind::Import && statement.kind != Statement::Kind::Event &&
                    statement.kind != Statement::Kind::Enum && statement.kind != Statement::Kind::Struct;
            }
            if (entry.empty() && main && !actions)
                options.entryFunction = "main";
            const auto result = execute(program, options, ioHost());
            for (const auto &line : result.output)
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
                const auto &bytes = native.machineCode(function);
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
    } catch (const std::exception &error) {
        std::cerr << "Samat error: " << error.what() << '\n';
        return 1;
    }
}
