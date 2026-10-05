#pragma once

#include "JMEngine/Scene/Scene.hpp"
#include "JMEngine/Script/Script.hpp"

#include <filesystem>
#include <string>

namespace jm {

struct ProjectSettings {
    int formatVersion{1};
    std::string projectId;
    std::string name{"My First Game"};
    std::string projectMode{"game"};
    std::string startupScene{"scenes/main.scene"};
};

struct ProjectDocument {
    ProjectSettings settings;
    Scene scene;
    ScriptDocument script;
};

class ProjectStore {
public:
    static ProjectSettings makeNewProject(std::string name);
    static void save(const std::filesystem::path& root, const ProjectSettings& settings,
                     const Scene& scene, const ScriptDocument& script);
    static ProjectDocument load(const std::filesystem::path& root);
};

} // namespace jm
