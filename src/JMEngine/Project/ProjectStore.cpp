#include "JMEngine/Project/ProjectStore.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace jm {
namespace {

using Json = nlohmann::json;

Json toJson(const Vec3& value) {
    return Json::array({value.x, value.y, value.z});
}

Vec3 readVec3(const Json& value, std::string_view field) {
    if (!value.is_array() || value.size() != 3) {
        throw std::runtime_error(std::string(field) + " must contain 3 numbers.");
    }
    Vec3 result{value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()};
    if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z)) {
        throw std::runtime_error(std::string(field) + " contains an invalid number.");
    }
    return result;
}

std::string makeUuid() {
    std::array<unsigned char, 16> bytes{};
    std::random_device random;
    for (unsigned char& byte : bytes) byte = static_cast<unsigned char>(random());
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);

    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(36);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) result.push_back('-');
        result.push_back(digits[bytes[i] >> 4]);
        result.push_back(digits[bytes[i] & 0x0F]);
    }
    return result;
}

void replaceFile(const std::filesystem::path& temporary, const std::filesystem::path& target) {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("Could not replace " + target.string() + " (Windows error " +
                                 std::to_string(GetLastError()) + ").");
    }
#else
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    if (error) throw std::runtime_error("Could not replace " + target.string() + ": " + error.message());
#endif
}

void writeJsonAtomically(const std::filesystem::path& path, const Json& document) {
    const std::filesystem::path temporary = path.wstring() + L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Could not write " + temporary.string());
        const std::string content = document.dump(2) + "\n";
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.flush();
        if (!output) throw std::runtime_error("Could not finish writing " + temporary.string());
    }
    replaceFile(temporary, path);
}

Json serializeObject(const GameObject& object) {
    return Json{
        {"id", object.id},
        {"kind", object.kind == ObjectKind::Cube3D ? "cube3d" : "sprite2d"},
        {"shape", object.shape == SpriteShape::Circle ? "circle" : "rectangle"},
        {"name", object.name},
        {"koreanName", object.koreanName},
        {"transform", {
            {"position", toJson(object.position)},
            {"rotationDegrees", toJson(object.rotationDegrees)},
            {"scale", toJson(object.scale)}
        }},
        {"color", toJson(object.color)},
        {"layer", object.layer},
        {"visible", object.visible},
        {"movementSpeed", object.movementSpeed},
        {"physics", {{"enabled", object.physicsEnabled}, {"static", object.isStatic},
                     {"mass", object.mass}, {"gravityScale", object.gravityScale}}},
        {"behaviors", {{"spinWhenPlaying", object.spinWhenPlaying}}}
    };
}

std::string eventName(ScriptEvent event, const std::string& key = {}) {
    switch (event) {
    case ScriptEvent::Start: return "start";
    case ScriptEvent::KeyRightHeld: return "key.right.held";
    case ScriptEvent::KeyLeftHeld: return "key.left.held";
    case ScriptEvent::KeySpacePressed: return "key.space.pressed";
    case ScriptEvent::KeyPressed: return "key." + key + ".pressed";
    case ScriptEvent::KeyHeld: return "key." + key + ".held";
    }
    return "start";
}

const char* actionName(ScriptAction action) {
    switch (action) {
    case ScriptAction::SetMovementSpeed: return "set_movement_speed";
    case ScriptAction::MoveHorizontal: return "move_horizontal";
    case ScriptAction::Jump: return "jump";
    case ScriptAction::CallFunction: return "call_function";
    }
    return "move_horizontal";
}

Json serializeScript(const ScriptDocument& script) {
    Json handlers = Json::array();
    const auto serializeStatements = [](const std::vector<ScriptStatement>& statements) {
        Json body = Json::array();
        for (const ScriptStatement& statement : statements) {
            body.push_back(Json{{"id", statement.nodeId}, {"action", actionName(statement.action)},
                                {"targetObjectId", statement.targetObjectId}, {"value", statement.value},
                                {"valueVariable", statement.valueVariable}, {"functionName", statement.functionName},
                                {"functionArguments", statement.functionArguments}});
        }
        return body;
    };
    for (const ScriptEventHandler& handler : script.handlers) {
        handlers.push_back(Json{{"id", handler.nodeId}, {"event", eventName(handler.event, handler.key)}, {"body", serializeStatements(handler.body)}});
    }
    Json variables = Json::array();
    for (const ScriptVariable& variable : script.variables)
        variables.push_back(Json{{"id", variable.nodeId}, {"name", variable.name}, {"value", variable.value}, {"constant", variable.constant}});
    Json functions = Json::array();
    for (const ScriptFunction& function : script.functions)
        functions.push_back(Json{{"id", function.nodeId}, {"name", function.name}, {"parameters", function.parameters},
                                 {"body", serializeStatements(function.body)}});
    return Json{{"formatVersion", script.formatVersion}, {"scriptId", script.scriptId},
                {"variables", std::move(variables)}, {"functions", std::move(functions)}, {"handlers", std::move(handlers)}};
}

ScriptDocument deserializeScriptV2(const Json& value) {
    ScriptDocument script;
    script.formatVersion = 3;
    script.scriptId = value.at("scriptId").get<std::string>();
    if (script.scriptId.empty() || !value.at("handlers").is_array())
        throw std::runtime_error("The Samat file contains an invalid ID or handler list.");
    if (const auto variables = value.find("variables"); variables != value.end()) {
        for (const Json& variableJson : *variables) {
            ScriptVariable variable{variableJson.at("id").get<std::string>(), variableJson.at("name").get<std::string>(),
                                   variableJson.at("value").get<float>(), variableJson.value("constant", false)};
            if (variable.nodeId.empty() || variable.name.empty() || !std::isfinite(variable.value))
                throw std::runtime_error("The Samat file contains an invalid variable.");
            script.variables.push_back(std::move(variable));
        }
    }
    const auto readStatements = [](const Json& bodyJson) {
        if (!bodyJson.is_array()) throw std::runtime_error("The Samat function body must be an array.");
        std::vector<ScriptStatement> statements;
        for (const Json& statementJson : bodyJson) {
            const std::string action = statementJson.at("action").get<std::string>();
            ScriptAction parsedAction;
            if (action == "set_movement_speed") parsedAction = ScriptAction::SetMovementSpeed;
            else if (action == "move_horizontal") parsedAction = ScriptAction::MoveHorizontal;
            else if (action == "jump") parsedAction = ScriptAction::Jump;
            else if (action == "call_function") parsedAction = ScriptAction::CallFunction;
            else throw std::runtime_error("The Samat file contains an unknown action.");
            ScriptStatement statement{statementJson.at("id").get<std::string>(), parsedAction,
                statementJson.value("targetObjectId", std::string{}), statementJson.at("value").get<float>(),
                statementJson.value("valueVariable", std::string{}), statementJson.value("functionName", std::string{})};
            statement.functionArguments = statementJson.value("functionArguments", std::vector<std::string>{});
            if (statement.nodeId.empty() || !std::isfinite(statement.value) ||
                (parsedAction != ScriptAction::CallFunction && statement.targetObjectId.empty()) ||
                (parsedAction == ScriptAction::CallFunction && statement.functionName.empty()))
                throw std::runtime_error("The Samat file contains an invalid statement.");
            statements.push_back(std::move(statement));
        }
        return statements;
    };
    if (const auto functions = value.find("functions"); functions != value.end()) {
        for (const Json& functionJson : *functions) {
            ScriptFunction function;
            function.nodeId = functionJson.at("id").get<std::string>();
            function.name = functionJson.at("name").get<std::string>();
            function.parameters = functionJson.value("parameters", std::vector<std::string>{});
            function.body = readStatements(functionJson.at("body"));
            if (function.nodeId.empty() || function.name.empty()) throw std::runtime_error("The Samat file contains an invalid function.");
            script.functions.push_back(std::move(function));
        }
    }
    for (const Json& handlerJson : value.at("handlers")) {
        ScriptEvent event{};
        std::string key;
        if (!parseScriptEvent(handlerJson.at("event").get<std::string>(), event, &key))
            throw std::runtime_error("The Samat file contains an unknown event.");
        ScriptEventHandler handler;
        handler.nodeId = handlerJson.at("id").get<std::string>();
        handler.event = event;
        handler.key = key;
        if (handler.nodeId.empty() || !handlerJson.at("body").is_array())
            throw std::runtime_error("The Samat file contains an invalid handler.");
        handler.body = readStatements(handlerJson.at("body"));
        script.handlers.push_back(std::move(handler));
    }
    return script;
}

GameObject deserializeObject(const Json& value) {
    if (!value.is_object()) throw std::runtime_error("Scene objects must be JSON objects.");
    GameObject object;
    object.id = value.at("id").get<std::string>();
    object.name = value.at("name").get<std::string>();
    object.koreanName = value.value("koreanName", object.name);
    if (object.id.empty() || object.name.empty()) throw std::runtime_error("Object ID and name cannot be empty.");

    const std::string kind = value.at("kind").get<std::string>();
    if (kind == "cube3d") object.kind = ObjectKind::Cube3D;
    else if (kind == "sprite2d") object.kind = ObjectKind::Sprite2D;
    else throw std::runtime_error("Unknown object kind: " + kind);
    const std::string shape = value.value("shape", std::string{"rectangle"});
    if (shape == "circle") object.shape = SpriteShape::Circle;
    else if (shape == "rectangle") object.shape = SpriteShape::Rectangle;
    else throw std::runtime_error("Unknown sprite shape: " + shape);

    const Json& transform = value.at("transform");
    object.position = readVec3(transform.at("position"), "transform.position");
    object.rotationDegrees = readVec3(transform.at("rotationDegrees"), "transform.rotationDegrees");
    object.scale = readVec3(transform.at("scale"), "transform.scale");
    object.color = readVec3(value.at("color"), "color");
    object.layer = value.value("layer", 1);
    object.visible = value.value("visible", true);
    if (object.layer < 0 || object.layer > 4) throw std::runtime_error("Object layer must be between 0 and 4.");
    object.movementSpeed = value.value("movementSpeed", 4.0F);
    if (!std::isfinite(object.movementSpeed) || object.movementSpeed < 0.0F || object.movementSpeed > 1000.0F) {
        throw std::runtime_error("Object movement speed must be between 0 and 1000.");
    }
    if (const auto physics = value.find("physics"); physics != value.end()) {
        object.physicsEnabled = physics->value("enabled", false);
        object.isStatic = physics->value("static", false);
        object.mass = physics->value("mass", 1.0F);
        object.gravityScale = physics->value("gravityScale", 1.0F);
    }
    if (!std::isfinite(object.mass) || object.mass <= 0.0F || !std::isfinite(object.gravityScale) ||
        object.gravityScale < 0.0F || object.gravityScale > 100.0F) {
        throw std::runtime_error("Physics mass must be positive and gravity scale must be between 0 and 100.");
    }
    for (float component : {object.scale.x, object.scale.y, object.scale.z}) {
        if (component <= 0.0F) throw std::runtime_error("Object scale must be greater than zero.");
    }
    for (float component : {object.color.x, object.color.y, object.color.z}) {
        if (component < 0.0F || component > 1.0F) throw std::runtime_error("Object color must be between 0 and 1.");
    }
    if (const auto behaviors = value.find("behaviors"); behaviors != value.end()) {
        object.spinWhenPlaying = behaviors->value("spinWhenPlaying", false);
    }
    return object;
}

Json readJson(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Could not open " + path.string());
    try {
        return Json::parse(input);
    } catch (const Json::exception& exception) {
        throw std::runtime_error("Invalid JSON in " + path.string() + ": " + exception.what());
    }
}

} // namespace

ProjectSettings ProjectStore::makeNewProject(std::string name) {
    ProjectSettings settings;
    settings.projectId = makeUuid();
    settings.name = name.empty() ? "My First Game" : std::move(name);
    return settings;
}

void ProjectStore::save(const std::filesystem::path& root, const ProjectSettings& settings,
                        const Scene& scene, const ScriptDocument& script) {
    if (root.empty()) throw std::runtime_error("Choose a project folder first.");
    if (settings.projectId.empty()) throw std::runtime_error("Project ID is missing.");
    if (settings.name.empty()) throw std::runtime_error("Project name cannot be empty.");

    std::filesystem::create_directories(root / "scenes");
    std::filesystem::create_directories(root / "scripts");
    Json objects = Json::array();
    for (const GameObject& object : scene.objects()) objects.push_back(serializeObject(object));

    const Json sceneDocument{
        {"formatVersion", 1},
        {"sceneId", "main"},
        {"name", scene.name()},
        {"backgroundColor", toJson(scene.backgroundColor())},
        {"objects", std::move(objects)}
    };
    const Json projectDocument{
        {"formatVersion", settings.formatVersion},
        {"engineVersion", "0.1.0-dev"},
        {"projectId", settings.projectId},
        {"name", settings.name},
        {"projectMode", settings.projectMode},
        {"profile", {{"workload", "2d-platformer"}, {"targetFps", 60}}},
        {"startupScene", settings.startupScene}
    };
    const Json scriptDocument = serializeScript(script);

    writeJsonAtomically(root / "scenes" / "main.scene", sceneDocument);
    writeJsonAtomically(root / "scripts" / "main.samat.json", scriptDocument);
    writeJsonAtomically(root / "project.jm", projectDocument);
}

ProjectDocument ProjectStore::load(const std::filesystem::path& root) {
    const Json project = readJson(root / "project.jm");
    const int projectVersion = project.at("formatVersion").get<int>();
    if (projectVersion != 1) throw std::runtime_error("This JM project format version is not supported.");
    if (project.value("startupScene", std::string{"scenes/main.scene"}) != "scenes/main.scene") {
        throw std::runtime_error("This prototype can only open scenes/main.scene.");
    }

    const Json scene = readJson(root / "scenes" / "main.scene");
    if (scene.at("formatVersion").get<int>() != 1) {
        throw std::runtime_error("This JM scene format version is not supported.");
    }
    if (!scene.at("objects").is_array()) throw std::runtime_error("Scene objects must be an array.");

    ProjectDocument result;
    result.settings.formatVersion = projectVersion;
    result.settings.projectId = project.at("projectId").get<std::string>();
    result.settings.name = project.at("name").get<std::string>();
    result.settings.projectMode = project.value("projectMode", std::string{"game"});
    result.settings.startupScene = "scenes/main.scene";
    if (result.settings.projectId.empty() || result.settings.name.empty()) {
        throw std::runtime_error("Project ID and name cannot be empty.");
    }

    std::vector<GameObject> objects;
    result.scene.setName(scene.value("name", std::string{"Main Scene"}));
    if (const auto background = scene.find("backgroundColor"); background != scene.end())
        result.scene.setBackgroundColor(readVec3(*background, "backgroundColor"));
    objects.reserve(scene.at("objects").size());
    std::unordered_set<std::string> objectIds;
    for (const Json& object : scene.at("objects")) {
        GameObject decoded = deserializeObject(object);
        if (!objectIds.insert(decoded.id).second) throw std::runtime_error("Scene contains duplicate object IDs.");
        objects.push_back(std::move(decoded));
    }
    result.scene.replaceObjects(std::move(objects));

    const std::filesystem::path scriptPath = root / "scripts" / "main.samat.json";
    if (std::filesystem::exists(scriptPath)) {
        const Json scriptDocument = readJson(scriptPath);
        const int scriptVersion = scriptDocument.at("formatVersion").get<int>();
        if (scriptVersion == 2 || scriptVersion == 3) {
            result.script = deserializeScriptV2(scriptDocument);
        } else if (scriptVersion == 1) {
            // Migrate the original single-move format into the shared event/action AST.
            const Json& handler = scriptDocument.at("nodes").at(0);
            const Json& move = handler.at("body").at(0);
            ScriptEvent event{};
            if (!parseScriptEvent(handler.at("event").get<std::string>(), event))
                throw std::runtime_error("The Samat v1 file contains an unknown event.");
            result.script.formatVersion = 3;
            result.script.scriptId = scriptDocument.at("scriptId").get<std::string>();
            result.script.handlers.push_back({handler.at("id").get<std::string>(), event,
                {{move.at("id").get<std::string>(), ScriptAction::MoveHorizontal,
                  move.at("targetObjectId").get<std::string>(), move.at("directionX").get<float>()}}});
        } else {
            throw std::runtime_error("This Samat document version is not supported.");
        }
        if (result.script.scriptId.empty()) throw std::runtime_error("The Samat file contains an invalid ID.");
    } else {
        result.script = makeDefaultScript(result.scene);
    }
    return result;
}

} // namespace jm
