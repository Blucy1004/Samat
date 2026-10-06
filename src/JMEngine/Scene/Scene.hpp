#pragma once

#include "JMEngine/Renderer/Matrix4.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace jm {

enum class ObjectKind {
    Cube3D,
    Sprite2D,
};

struct GameObject {
    std::string id;
    std::uint64_t generation{};
    ObjectKind kind{ObjectKind::Cube3D};
    std::string name;
    std::string koreanName;
    Vec3 position{0.0F, 0.0F, 0.0F};
    Vec3 rotationDegrees{0.0F, 0.0F, 0.0F};
    Vec3 scale{1.0F, 1.0F, 1.0F};
    Vec3 color{1.0F, 1.0F, 1.0F};
    int layer{1};
    bool visible{true};
    float movementSpeed{4.0F};
    bool physicsEnabled{false};
    bool isStatic{false};
    float mass{1.0F};
    float gravityScale{1.0F};
    float horizontalVelocity{0.0F};
    float verticalVelocity{0.0F};
    bool grounded{false};
    bool spinWhenPlaying{false};
};

class Scene {
  public:
    Scene();
    Scene(const Scene &other);
    Scene &operator=(const Scene &other);
    Scene(Scene &&) noexcept = default;
    Scene &operator=(Scene &&) noexcept = default;
    std::uint64_t identity() const { return identity_; }
    void invalidateReferences();

    GameObject &create(ObjectKind kind);
    void deleteSelected();
    void select(std::string_view id);
    void selectFirst(ObjectKind kind);
    void replaceObjects(std::vector<GameObject> objects);
    void stepPhysics2D(float fixedDeltaSeconds);
    GameObject *selected();
    const GameObject *selected() const;

    std::vector<GameObject> &objects() { return objects_; }
    const std::vector<GameObject> &objects() const { return objects_; }

  private:
    std::vector<GameObject> objects_;
    std::uint64_t nextId_{1};
    std::uint64_t identity_{};
    std::string selectedId_;
};

} // namespace jm
