#include "JMEngine/Scene/Scene.hpp"

#include <algorithm>
#include <atomic>
#include <utility>

namespace jm {
namespace {
std::atomic<std::uint64_t> referenceToken{1};
}
void Scene::invalidateReferences() { identity_ = referenceToken.fetch_add(1); }

Scene::Scene(const Scene &other)
    : objects_(other.objects_), nextId_(other.nextId_), selectedId_(other.selectedId_) {
    invalidateReferences();
}
Scene &Scene::operator=(const Scene &other) {
    if (this != &other) {
        auto objects = other.objects_;
        objects_.swap(objects);
        nextId_ = other.nextId_;
        selectedId_ = other.selectedId_;
        invalidateReferences();
    }
    return *this;
}

Scene::Scene() {
    invalidateReferences();
    GameObject &cube = create(ObjectKind::Cube3D);
    cube.name = "Cube";
    cube.koreanName = "큐브";
    GameObject &player = create(ObjectKind::Sprite2D);
    player.name = "Player";
    player.koreanName = "플레이어";
    player.position = {0.0F, -2.2F, 0.0F};
    player.scale = {0.75F, 0.9F, 1.0F};
    player.color = {0.28F, 0.76F, 1.0F};
    player.movementSpeed = 8.0F;
    player.physicsEnabled = true;
    player.layer = 2;

    GameObject &ground = create(ObjectKind::Sprite2D);
    ground.name = "Ground";
    ground.koreanName = "바닥";
    ground.position = {0.0F, -3.25F, 0.0F};
    ground.scale = {30.0F, 0.45F, 1.0F};
    ground.color = {0.28F, 0.34F, 0.43F};
    ground.physicsEnabled = true;
    ground.isStatic = true;
    ground.layer = 0;
    selectFirst(ObjectKind::Sprite2D);
}

GameObject &Scene::create(ObjectKind kind) {
    GameObject object{};
    object.generation = referenceToken.fetch_add(1);
    object.id = "object-" + std::to_string(nextId_++);
    object.kind = kind;
    object.name = kind == ObjectKind::Cube3D ? "Cube " : "Sprite ";
    object.name += std::to_string(objects_.size() + 1);
    object.koreanName = kind == ObjectKind::Cube3D ? "큐브" : "스프라이트";
    const float column = static_cast<float>(objects_.size() % 5) - 2.0F;
    if (kind == ObjectKind::Cube3D) {
        object.scale = {0.85F, 0.85F, 0.85F};
        object.position.x = objects_.empty() ? 0.0F : column * 1.4F;
    } else {
        object.scale = {1.25F, 1.25F, 1.0F};
        object.position.x = objects_.empty() ? 0.0F : column * 1.7F;
        object.color = {0.75F, 0.92F, 1.0F};
    }

    objects_.push_back(std::move(object));
    selectedId_ = objects_.back().id;
    return objects_.back();
}

void Scene::deleteSelected() {
    for (auto iterator = objects_.begin(); iterator != objects_.end(); ++iterator) {
        if (iterator->id == selectedId_) {
            objects_.erase(iterator);
            selectedId_ = objects_.empty() ? std::string{} : objects_.back().id;
            return;
        }
    }
}

void Scene::select(std::string_view id) {
    for (const auto &object : objects_) {
        if (object.id == id) {
            selectedId_ = object.id;
            return;
        }
    }
}

void Scene::replaceObjects(std::vector<GameObject> objects) {
    invalidateReferences();
    for (GameObject &object : objects) {
        object.generation = referenceToken.fetch_add(1);
        object.horizontalVelocity = 0.0F;
        object.verticalVelocity = 0.0F;
        object.grounded = false;
    }
    objects_ = std::move(objects);
    nextId_ = 1;
    selectedId_.clear();
    for (const GameObject &object : objects_) {
        if (object.id.rfind("object-", 0) == 0) {
            try {
                nextId_ = std::max<std::uint64_t>(nextId_, std::stoull(object.id.substr(7)) + 1);
            } catch (...) {
                // Non-numeric stable IDs are valid; they do not affect generated demo IDs.
            }
        }
    }
    if (!objects_.empty())
        selectedId_ = objects_.back().id;
}

void Scene::stepPhysics2D(float fixedDeltaSeconds) {
    if (fixedDeltaSeconds <= 0.0F)
        return;
    for (GameObject &body : objects_) {
        if (body.kind != ObjectKind::Sprite2D || !body.physicsEnabled || body.isStatic)
            continue;
        const float previousBottom = body.position.y - body.scale.y * 0.5F;
        body.grounded = false;
        body.verticalVelocity -= 20.0F * body.gravityScale * fixedDeltaSeconds;
        body.position.x += body.horizontalVelocity * fixedDeltaSeconds;
        body.position.y += body.verticalVelocity * fixedDeltaSeconds;
        const float currentBottom = body.position.y - body.scale.y * 0.5F;

        for (const GameObject &surface : objects_) {
            if (&surface == &body || surface.kind != ObjectKind::Sprite2D || !surface.physicsEnabled ||
                !surface.isStatic)
                continue;
            const float surfaceTop = surface.position.y + surface.scale.y * 0.5F;
            const bool overlapsX =
                body.position.x + body.scale.x * 0.5F > surface.position.x - surface.scale.x * 0.5F &&
                body.position.x - body.scale.x * 0.5F < surface.position.x + surface.scale.x * 0.5F;
            const bool crossedTop =
                body.verticalVelocity <= 0.0F && currentBottom <= surfaceTop && previousBottom >= surfaceTop;
            if (!overlapsX || !crossedTop)
                continue;
            body.position.y = surfaceTop + body.scale.y * 0.5F;
            body.verticalVelocity = 0.0F;
            body.grounded = true;
            break;
        }
    }
}

void Scene::selectFirst(ObjectKind kind) {
    for (const auto &object : objects_) {
        if (object.kind == kind) {
            selectedId_ = object.id;
            return;
        }
    }
    selectedId_.clear();
}

GameObject *Scene::selected() {
    for (auto &object : objects_) {
        if (object.id == selectedId_)
            return &object;
    }
    return nullptr;
}

const GameObject *Scene::selected() const {
    for (const auto &object : objects_) {
        if (object.id == selectedId_)
            return &object;
    }
    return nullptr;
}

} // namespace jm
