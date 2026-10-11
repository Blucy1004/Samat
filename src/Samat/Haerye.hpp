#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jm {

struct HaeryeColor {
    float r{};
    float g{};
    float b{};
    friend bool operator==(HaeryeColor, HaeryeColor) = default;
};

enum class HaeryeShape {
    Rectangle,
    Circle,
    Unknown,
};

enum class HaeryeGameMode { Platformer };
enum class HaeryeRole { Player, Platform, Goal, Hazard };

struct HaeryeObject {
    std::string name;
    std::size_t sourceLine{1};
    std::size_t sourceColumn{1};
    std::optional<HaeryeShape> shape;
    std::optional<std::array<float, 2>> position;
    std::optional<std::array<float, 2>> size;
    std::optional<float> rotation;
    std::optional<HaeryeColor> color;
    std::optional<HaeryeRole> role;
    friend bool operator==(const HaeryeObject &left, const HaeryeObject &right) {
        return left.name == right.name && left.shape == right.shape && left.position == right.position &&
               left.size == right.size && left.rotation == right.rotation && left.color == right.color &&
               left.role == right.role;
    }
};

struct HaeryeDocument {
    std::string name;
    HaeryeColor backgroundColor{14.0F / 255.0F, 18.0F / 255.0F, 27.0F / 255.0F};
    std::vector<HaeryeObject> objects;
    std::optional<HaeryeGameMode> gameMode;
    friend bool operator==(const HaeryeDocument &left, const HaeryeDocument &right) {
        return left.name == right.name && left.backgroundColor == right.backgroundColor &&
               left.objects == right.objects && left.gameMode == right.gameMode;
    }
};

struct HaeryeDiagnostic {
    std::size_t line{};
    std::size_t column{};
    std::string message;
    std::string toString() const;
};

bool parseHaerye(std::string_view source, HaeryeDocument &document, HaeryeDiagnostic &diagnostic);
bool validateHaerye(const HaeryeDocument &document, HaeryeDiagnostic &diagnostic);
std::string serializeHaerye(const HaeryeDocument &document);

} // namespace jm
