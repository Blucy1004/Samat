#include "ModuleLoader.hpp"
#include <unordered_set>
namespace jm::script {
bool loadModules(const ModuleSource &entry, const SourceResolver &resolver, Program &output,
                 Diagnostic &diagnostic, size_t limit) {
    try {
        Program result;
        std::unordered_set<std::string> active, done;
        std::vector<std::string> stack;
        size_t bytes = 0;
        std::function<void(const ModuleSource &)> visit = [&](const ModuleSource &source) {
            if (done.contains(source.identity))
                return;
            if (active.contains(source.identity)) {
                std::string chain;
                for (const auto &name : stack)
                    chain += name + " -> ";
                throw std::runtime_error("JM2008: Circular file import: " + chain + source.identity);
            }
            if (active.size() + done.size() >= limit || (bytes += source.source.size()) > 16 * 1024 * 1024)
                throw std::runtime_error("JM2008: Module count/source budget exceeded.");
            active.insert(source.identity);
            stack.push_back(source.identity);
            Program module;
            Diagnostic code, korean;
            if (!parseCode(source.source, module, code) && !parseKorean(source.source, module, korean))
                throw std::runtime_error("JM2008: Module parse failed in " + source.identity + ":" +
                                         std::to_string(code.line) + ": " + code.message);
            for (const auto &statement : module.statements)
                if (statement.kind == Statement::Kind::Import && statement.fileImport) {
                    auto dependency = resolver ? resolver(source.identity, statement.name) : std::nullopt;
                    if (!dependency)
                        throw std::runtime_error("JM2008: Cannot resolve file import '" + statement.name +
                                                 "' from " + source.identity);
                    visit(*dependency);
                }
            for (auto &statement : module.statements)
                if (statement.kind != Statement::Kind::Import || !statement.fileImport)
                    result.statements.push_back(std::move(statement));
            stack.pop_back();
            active.erase(source.identity);
            done.insert(source.identity);
        };
        visit(entry);
        std::vector<Diagnostic> errors;
        if (!check(result, errors)) {
            diagnostic = errors.front();
            return false;
        }
        output = std::move(result);
        diagnostic = {};
        return true;
    } catch (const std::exception &error) {
        diagnostic = {error.what()};
        diagnostic.code = "JM2008";
        return false;
    }
}
} // namespace jm::script
