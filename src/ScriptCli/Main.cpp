#include "JMEngine/Script/LanguageCore.hpp"
#include "JMEngine/Script/JMIR.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <iomanip>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: Samat <file.st> | --ir <file.st> | --native <function> <file.st> [i64 args...]\n";
        return 2;
    }

    std::string mode="--run", function;
    int fileArgument=1, firstNativeArgument=argc;
    if(std::string(argv[1])=="--ir") { mode="--ir"; fileArgument=2; }
    else if(std::string(argv[1])=="--native") { mode="--native"; if(argc<4) { std::cerr << "Usage: Samat --native <function> <file.st> [i64 args...]\n"; return 2; } function=argv[2]; fileArgument=3; firstNativeArgument=4; }
    if(fileArgument>=argc) { std::cerr << "Missing Samat input file.\n"; return 2; }

    std::ifstream input(argv[fileArgument], std::ios::binary);
    if (!input) {
        std::cerr << "Could not open script: " << argv[fileArgument] << '\n';
        return 2;
    }

    const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    jm::script::Program program;
    jm::script::Diagnostic diagnostic;
    if (!jm::script::parseCode(source, program, diagnostic) && !jm::script::parseKorean(source, program, diagnostic)) {
        std::cerr << "Parse error";
        if (diagnostic.line != 0) std::cerr << " on line " << diagnostic.line;
        std::cerr << ": " << diagnostic.message << '\n';
        return 1;
    }

    if(mode=="--ir") {
        jm::script::ir::Module module; jm::script::ir::LoweringDiagnostic lowerDiagnostic;
        if(!jm::script::ir::lower(program,module,lowerDiagnostic)) { std::cerr << "Native lowering error: " << lowerDiagnostic.message << '\n'; return 1; }
        std::cout << jm::script::ir::format(module); return 0;
    }

    try {
        if(mode=="--native") {
            jm::script::ir::Module module; jm::script::ir::LoweringDiagnostic lowerDiagnostic;
            if(!jm::script::ir::lower(program,module,lowerDiagnostic)) throw std::runtime_error(lowerDiagnostic.message);
            std::vector<std::int64_t> arguments;
            for(int i=firstNativeArgument;i<argc;++i) arguments.push_back(std::stoll(argv[i]));
            jm::script::ir::X64Backend backend;
            auto native=backend.compile(module);
            const auto result=native.invoke(function,arguments);
            std::cout << "target: " << backend.targetTriple() << "\nresult: " << result << "\nmachine code (" << native.machineCode(function).size() << " bytes):\n";
            const auto& bytes=native.machineCode(function);
            for(std::size_t i=0;i<bytes.size();++i) { if(i%16==0) std::cout << "\n"; std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(bytes[i]) << ' '; }
            std::cout << std::dec << '\n'; return 0;
        }
        const jm::script::ExecutionResult result = jm::script::execute(program);
        for (const std::string& line : result.output) std::cout << line << '\n';
    } catch (const std::exception& error) {
        std::cerr << "Runtime error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
