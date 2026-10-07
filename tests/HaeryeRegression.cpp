#include "JMEngine/Project/ProjectStore.hpp"
#include "JMEngine/Scene/Haerye.hpp"
#include "JMEngine/Script/EngineScriptAPI.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::string readFile(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Could not read " + path.string());
    return {std::istreambuf_iterator<char>(input), {}};
}
jm::HaeryeDocument parseRaw(std::string_view source) {
    jm::HaeryeDocument document;
    jm::HaeryeDiagnostic diagnostic;
    require(jm::parseHaerye(source, document, diagnostic), diagnostic.toString());
    return document;
}
jm::HaeryeDocument parseValid(std::string_view source) {
    auto document = parseRaw(source);
    jm::HaeryeDiagnostic diagnostic;
    require(jm::validateHaerye(document, diagnostic), diagnostic.toString());
    return document;
}
void expectParseError(std::string_view source, std::string_view message, std::size_t line = 0) {
    jm::HaeryeDocument document;
    jm::HaeryeDiagnostic diagnostic;
    require(!jm::parseHaerye(source, document, diagnostic), "Invalid Haerye source was accepted.");
    require(diagnostic.message.find(message) != std::string::npos,
            "Unexpected Haerye diagnostic: " + diagnostic.toString());
    if (line)
        require(diagnostic.line == line, "Haerye diagnostic reported the wrong source line.");
}
void expectValidationError(const jm::HaeryeDocument &document, std::string_view message) {
    jm::HaeryeDiagnostic diagnostic;
    require(!jm::validateHaerye(document, diagnostic), "Invalid Haerye document was accepted.");
    require(diagnostic.message.find(message) != std::string::npos,
            "Unexpected Haerye validation diagnostic: " + diagnostic.toString());
}
jm::GameObject &findObject(jm::Scene &scene, std::string_view name) {
    auto found = std::find_if(scene.objects().begin(), scene.objects().end(),
                              [&](const auto &object) { return object.name == name; });
    if (found == scene.objects().end())
        throw std::runtime_error("Missing object in instantiated Scene: " + std::string(name));
    return *found;
}
void parserAndDiagnostics() {
    const auto document = parseValid(R"HY(// comments are ignored
scene "Minimal" {
    background: "#101014"
    object "round" {
        shape: circle
        position: (-1.5, 2)
        size: (0.5, 0.5)
        rotation: 30
        color: "#20A0FF"
    }
})HY");
    require(document.name == "Minimal" && document.objects.size() == 1, "Scene and object parse.");
    require(document.objects[0].shape == jm::HaeryeShape::Circle && document.objects[0].position &&
                (*document.objects[0].position)[0] == -1.5F && document.objects[0].rotation == 30.0F,
            "Shape and transform values parse.");

    expectParseError("scene \"x\" {\n  object \"o\" {\n    postion: (0, 0)\n  }\n}\n",
                     "Unknown object property", 3);
    expectParseError("scene \"x\" {\n    postion: (0, 0)\n}\n", "Unknown scene property", 2);
    expectParseError("scene \"x\" {\n object \"o\" { shape: rectangle position: (0, 0) size: (1, 1) color: \"#fff\" }\n}\n",
                     "#RRGGBB");
    expectParseError("scene \"x\" { object \"o\" { color: \"#GGGGGG\" } }", "non-hexadecimal");
    expectParseError("scene \"x\" {\n object \"o\" { shape: rectangle position: (0 0) }\n}\n", "Expected ','");
    expectParseError("scene \"x\" {\n object \"o\" { sprite: \"assets/a.png\" }\n}\n",
                     "Asset references are unsupported");
    expectParseError("scene \"x\" { object \"o\" { shape: rectangle shape: circle } }",
                     "Duplicate object property");
    expectParseError("scene \"x\" {\n  object \"o\" {\n    shape rectangle\n  }\n}\n", "Expected ':'", 3);
    expectParseError("scene \"x\" {}\nscene \"y\" {}", "only one scene");

    auto duplicate = parseRaw(R"HY(scene "x" {
 object "same" { shape: rectangle position: (0, 0) size: (1, 1) color: "#FFFFFF" }
 object "same" { shape: circle position: (1, 1) size: (1, 1) color: "#FFFFFF" }
})HY");
    expectValidationError(duplicate, "Duplicate object name");
    auto missing = parseRaw("scene \"x\" { object \"o\" { shape: rectangle } }");
    expectValidationError(missing, "needs shape, position, size, and color");
    auto invalidShape = parseRaw("scene \"x\" { object \"o\" { shape: triangle } }");
    expectValidationError(invalidShape, "shape rectangle or circle");
    auto noRotation = parseValid("scene \"x\" { object \"o\" { shape: rectangle position: (0, 0) size: (1, 1) color: \"#FFFFFF\" } }");
    require(parseValid(jm::serializeHaerye(noRotation)) == noRotation,
            "Omitted rotation should remain semantically omitted after round trip.");
}

void roundTripAndInstantiation(const jm::HaeryeDocument &document) {
    const std::string serialized = jm::serializeHaerye(document);
    const auto reparsed = parseValid(serialized);
    require(reparsed == document, "Haerye semantic serialize/parse round trip changed the document.");

    jm::Scene scene;
    const auto previousIdentity = scene.identity();
    jm::HaeryeDiagnostic diagnostic;
    require(jm::instantiateHaerye(document, scene, diagnostic), diagnostic.toString());
    require(scene.identity() != previousIdentity, "Scene instantiation did not invalidate old Entity references.");
    require(scene.name() == document.name && scene.objects().size() == 1, "Haerye Scene metadata and objects.");
    require(scene.backgroundColor().x == document.backgroundColor.x, "Haerye background color applied.");
    auto &object = findObject(scene, "round");
    require(object.kind == jm::ObjectKind::Sprite2D && object.shape == jm::SpriteShape::Circle,
            "Haerye circle maps to existing Sprite2D rendering object.");
    require(object.id == "haerye:round", "Haerye object gets a stable ID derived from its declared name.");
    require(std::abs(object.position.x + 1.5F) < 1e-6F && std::abs(object.scale.x - 0.5F) < 1e-6F &&
                std::abs(object.rotationDegrees.z - 30.0F) < 1e-6F,
            "Haerye transform maps to existing Scene object.");
}

void pongIntegration(const std::filesystem::path &root) {
    const auto document = parseValid(readFile(root / "examples/Pong/pong.hy"));
    const auto source = readFile(root / "examples/Pong/pong.st");
    jm::script::Program program;
    jm::script::Diagnostic scriptDiagnostic;
    require(jm::script::parseCode(source, program, scriptDiagnostic), scriptDiagnostic.message);
    auto metadata = jm::engineNativeFunctions();
    std::vector<jm::script::Diagnostic> typeErrors;
    const bool typeChecked = jm::script::check(program, typeErrors, &metadata);
    require(typeChecked, typeErrors.empty() ? "Pong type check failed without diagnostic."
                                            : typeErrors.front().code + ": " + typeErrors.front().message);

    jm::Scene scene;
    jm::HaeryeDiagnostic diagnostic;
    require(jm::instantiateHaerye(document, scene, diagnostic), diagnostic.toString());
    require(scene.objects().size() == 3 && findObject(scene, "ball").shape == jm::SpriteShape::Circle,
            "Pong Haerye declares both paddles and a circular ball.");
    jm::EngineEventRuntime runtime(scene, {}, program);
    runtime.start();
    jm::ScriptInput input;
    input.keysHeld.insert("w");
    runtime.tick(input, 0.1);
    require(std::abs(findObject(scene, "player").position.y - 0.8F) < 1e-5F,
            "Samat scene.find moves an object declared by Haerye.");

    const auto safeProgram = R"ST(import jm.game
let saved: Entity? = null
on start:
    saved = scene.find("player")
on update:
    if saved != null:
        saved.setPosition(Vector2(1, 1))
)ST";
    jm::script::Program safe;
    require(jm::script::parseCode(safeProgram, safe, scriptDiagnostic), scriptDiagnostic.message);
    jm::Scene safeScene;
    require(jm::instantiateHaerye(document, safeScene, diagnostic), diagnostic.toString());
    jm::EngineEventRuntime staleRuntime(safeScene, {}, safe);
    staleRuntime.start();
    require(jm::instantiateHaerye(document, safeScene, diagnostic), diagnostic.toString());
    bool staleRejected = false;
    try {
        staleRuntime.tick({}, 0.1);
    } catch (const std::exception &error) {
        staleRejected = std::string(error.what()).find("stale") != std::string::npos ||
                        std::string(error.what()).find("Scene") != std::string::npos;
    }
    require(staleRejected, "Haerye re-instantiation must preserve stale Entity protection.");
}

void projectPersistence(const jm::HaeryeDocument &document) {
    const auto folder = std::filesystem::temp_directory_path() / "jm-haerye-project-regression";
    std::filesystem::remove_all(folder);
    jm::Scene scene;
    jm::HaeryeDiagnostic diagnostic;
    require(jm::instantiateHaerye(document, scene, diagnostic), diagnostic.toString());
    auto settings = jm::ProjectStore::makeNewProject("Haerye persistence");
    jm::ProjectStore::save(folder, settings, scene, jm::makeDefaultScript(scene));
    auto loaded = jm::ProjectStore::load(folder);
    require(loaded.scene.name() == document.name && loaded.scene.backgroundColor().x == scene.backgroundColor().x,
            "Existing project Scene serialization preserves Haerye metadata.");
    require(findObject(loaded.scene, "round").shape == jm::SpriteShape::Circle,
            "Existing project Scene serialization preserves primitive shape.");
    std::filesystem::remove_all(folder);
}
} // namespace

int main() {
    try {
        parserAndDiagnostics();
        auto document = parseValid(R"HY(scene "Minimal" {
    background: "#101014"
    object "round" {
        shape: circle
        position: (-1.5, 2)
        size: (0.5, 0.5)
        rotation: 30
        color: "#20A0FF"
    }
})HY");
        roundTripAndInstantiation(document);
        const std::filesystem::path root = JM_SOURCE_DIR;
        pongIntegration(root);
        projectPersistence(document);
        std::cout << "Haerye parser, strict validation, round trip, Scene integration, safe Entity lookup, "
                     "and Pong integration PASS.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Haerye regression failed: " << error.what() << '\n';
        return 1;
    }
}
