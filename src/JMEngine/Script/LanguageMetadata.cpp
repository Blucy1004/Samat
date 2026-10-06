#include "JMEngine/Script/JMIR.hpp"
#include <algorithm>
#include <cctype>
namespace jm::script::ir {
namespace {
std::string initials(std::string_view text) {
    static const char *consonants[] = {"ㄱ", "ㄲ", "ㄴ", "ㄷ", "ㄸ", "ㄹ", "ㅁ", "ㅂ", "ㅃ", "ㅅ",
                                       "ㅆ", "ㅇ", "ㅈ", "ㅉ", "ㅊ", "ㅋ", "ㅌ", "ㅍ", "ㅎ"};
    std::string result;
    for (size_t i = 0; i < text.size();) {
        auto first = static_cast<unsigned char>(text[i]);
        if (first < 128) {
            result += static_cast<char>(std::tolower(first));
            ++i;
            continue;
        }
        if (first >= 0xe0 && first <= 0xef && i + 2 < text.size()) {
            auto second = static_cast<unsigned char>(text[i + 1]),
                 third = static_cast<unsigned char>(text[i + 2]);
            if ((second & 0xc0) != 0x80 || (third & 0xc0) != 0x80) {
                ++i;
                continue;
            }
            auto cp = ((first & 15) << 12) | ((second & 63) << 6) | (third & 63);
            if (cp >= 0xac00 && cp <= 0xd7a3)
                result += consonants[(cp - 0xac00) / 588];
            else
                result.append(text.substr(i, 3));
            i += 3;
        } else {
            result += text[i++];
        }
    }
    return result;
}
} // namespace
void prepareToolingMetadata(NativeFunctionRegistry::Metadata &metadata) {
    auto &tool = metadata.tooling;
    if (tool.category.empty())
        tool.category = metadata.symbol.starts_with("builtin.player.")  ? "움직임"
                        : metadata.symbol.starts_with("builtin.input.") ? "입력"
                        : metadata.symbol.starts_with("builtin.scene.") ? "장면"
                                                                        : "고급";
    tool.advanced = tool.category == "고급";
    if (tool.beginnerName.empty())
        tool.beginnerName = metadata.koreanName;
    if (tool.parameters.empty())
        for (size_t i = 0; i < metadata.parameterTypes.size(); ++i) {
            NativeFunctionRegistry::ParameterEditor editor;
            editor.label = metadata.parameterNames[i];
            editor.numeric =
                metadata.parameterTypes[i] == Type::Int || metadata.parameterTypes[i] == Type::Float;
            editor.minimum = 0;
            editor.maximum = 30;
            editor.initial = editor.label == "force"       ? 12
                             : editor.label == "speed"     ? 8
                             : editor.label == "direction" ? 1
                                                           : 0;
            editor.recommendedMinimum = editor.label == "force" ? 8 : 0;
            editor.recommendedMaximum = editor.label == "force" ? 16 : 30;
            editor.step = metadata.parameterTypes[i] == Type::Float ? 0.5 : 1;
            if (editor.label == "force") {
                editor.label = "점프 힘";
                editor.presets = {{"약하게", 8}, {"보통", 12}, {"강하게", 20}};
                tool.beginnerName = "점프하기";
            }
            tool.parameters.push_back(editor);
        }
    if (metadata.symbol == "builtin.scene.find") {
        tool.beginnerName = "찾았다면";
        tool.codeTemplate = "on start:\n    let enemy: Entity? = scene.find(\"enemy\")\n    if enemy != "
                            "null:\n        enemy.position += Vector2(1.0, 0.0)\n";
    }
}
bool metadataMatches(const NativeFunctionRegistry::Metadata &metadata, std::string_view query) {
    if (query.empty())
        return true;
    auto text = metadata.displayName + " " + metadata.koreanName + " " + metadata.tooling.beginnerName + " " +
                metadata.documentation;
    return text.find(query) != std::string::npos || initials(text).find(initials(query)) != std::string::npos;
}
std::string metadataSignature(const NativeFunctionRegistry::Metadata &metadata) {
    std::string result = metadata.displayName + "(";
    for (size_t i = 0; i < metadata.parameterNames.size(); ++i) {
        if (i)
            result += ", ";
        result += metadata.parameterNames[i] + ": " +
                  annotationName(metadata.parameterTypes[i], i < metadata.parameterElementTypes.size()
                                                                 ? metadata.parameterElementTypes[i]
                                                                 : Type::Any);
    }
    return result + ") -> " + annotationName(metadata.returnType, metadata.returnElementType);
}
std::string metadataTemplate(const NativeFunctionRegistry::Metadata &metadata, bool korean) {
    auto source = metadata.tooling.codeTemplate;
    if (source.empty()) {
        source = "on key.space.pressed:\n    " + metadata.displayName + "(";
        for (size_t i = 0; i < metadata.parameterNames.size(); ++i) {
            if (i)
                source += ", ";
            source += metadata.parameterNames[i] + ": ";
            auto type = metadata.parameterTypes[i];
            if (type == Type::String)
                source += "\"right\"";
            else if (type == Type::Vector2)
                source += "Vector2(0.0, 0.0)";
            else if (type == Type::Vector3)
                source += "Vector3(0.0, 0.0, 0.0)";
            else if (type == Type::Color)
                source += "Color(1.0, 1.0, 1.0, 1.0)";
            else if (type == Type::Bool)
                source += "true";
            else if (type == Type::Entity || type == Type::Optional || type == Type::List)
                return {};
            else {
                auto value =
                    i < metadata.tooling.parameters.size() ? metadata.tooling.parameters[i].initial : 0;
                source +=
                    type == Type::Float ? std::to_string(value) : std::to_string(static_cast<int64_t>(value));
            }
        }
        source += ")\n";
    }
    Program program;
    Diagnostic diagnostic;
    if (!parseCode(source, program, diagnostic))
        throw std::runtime_error("Invalid metadata template: " + diagnostic.message);
    return korean ? renderKorean(program) : renderCode(program);
}
} // namespace jm::script::ir
