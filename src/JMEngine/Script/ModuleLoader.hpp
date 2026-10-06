#pragma once
#include "LanguageCore.hpp"
#include <optional>
namespace jm::script {
struct ModuleSource {
    std::string identity, source;
};
using SourceResolver =
    std::function<std::optional<ModuleSource>(std::string_view importer, std::string_view request)>;
// Dependency-first, once-per-identity initialization. File access is supplied by the host.
bool loadModules(const ModuleSource &entry, const SourceResolver &resolver, Program &output,
                 Diagnostic &diagnostic, size_t limit = 128);
} // namespace jm::script
