#pragma once
#include "Samat/Language/LanguageCore.hpp"
#include <optional>

namespace jm::script {
struct StandardFunction {
    std::string symbol, module, documentation;
    std::size_t minimumArity{}, maximumArity{};
    Type returnType{Type::Float};
    bool numericArguments{true};
};
int runtimeStandardOperation(std::string_view name);
bool standardModule(std::string_view identity);
std::optional<StandardFunction> standardFunction(std::string_view name);
} // namespace jm::script
