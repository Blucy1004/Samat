#pragma once
#include "JMEngine/Script/LanguageCore.hpp"
#include <optional>

namespace jm::script {
struct StandardFunction {
    std::string symbol, module, documentation;
    std::size_t minimumArity{}, maximumArity{};
    Type returnType{Type::Float};
    bool numericArguments{true};
};
bool standardModule(std::string_view identity);
std::optional<StandardFunction> standardFunction(std::string_view name);
} // namespace jm::script
