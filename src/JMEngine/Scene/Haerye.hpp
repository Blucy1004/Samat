#pragma once

#include "JMEngine/Scene/Scene.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jm {

enum class HaeryeShape {
    Rectangle,
    Circle,
    Unknown,
};

struct HaeryeObject {
    std::string name;
    std::size_t sourceLine{1};
    std::size_t sourceColumn{1};
    std::optional<HaeryeShape> shape;
    std::optional<std::array<float, 2>> position;
    std::optional<std::array<float, 2>> size;
    std::optional<float> rotation;
    std::optional<Vec3> color;
    friend bool operator==(const HaeryeObject &left, const HaeryeObject &right) {
        const bool sameColor = left.color.has_value() == right.color.has_value() &&
                               (!left.color || (left.color->x == right.color->x &&
                                                left.color->y == right.color->y &&
                                                left.color->z == right.color->z));
        return left.name == right.name && left.shape == right.shape && left.position == right.position &&
               left.size == right.size && left.rotation == right.rotation && sameColor;
    }
};

struct HaeryeDocument {
    std::string name;
    Vec3 backgroundColor{14.0F / 255.0F, 18.0F / 255.0F, 27.0F / 255.0F};
    std::vector<HaeryeObject> objects;
    friend bool operator==(const HaeryeDocument &left, const HaeryeDocument &right) {
        return left.name == right.name && left.backgroundColor.x == right.backgroundColor.x &&
               left.backgroundColor.y == right.backgroundColor.y &&
               left.backgroundColor.z == right.backgroundColor.z && left.objects == right.objects;
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
bool instantiateHaerye(const HaeryeDocument &document, Scene &scene, HaeryeDiagnostic &diagnostic);
std::string serializeHaerye(const HaeryeDocument &document);

} // namespace jm
