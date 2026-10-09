#include "Samat/Haerye.hpp"

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

jm::HaeryeDocument parseValid(std::string_view source) {
    jm::HaeryeDocument document;
    jm::HaeryeDiagnostic diagnostic;
    require(jm::parseHaerye(source, document, diagnostic), diagnostic.toString());
    require(jm::validateHaerye(document, diagnostic), diagnostic.toString());
    return document;
}

void expectInvalid(std::string_view source, std::string_view expected) {
    jm::HaeryeDocument document;
    jm::HaeryeDiagnostic diagnostic;
    const bool parsed = jm::parseHaerye(source, document, diagnostic);
    require(!parsed || !jm::validateHaerye(document, diagnostic), "Invalid scene was accepted.");
    require(diagnostic.message.find(expected) != std::string::npos,
            "Unexpected Haerye diagnostic: " + diagnostic.toString());
}

} // namespace

int main() {
    try {
        const auto root = std::filesystem::path(SAMAT_SOURCE_DIR);
        const auto source = readFile(root / "examples/Haerye/pong.hy");
        const auto scene = parseValid(source);
        require(scene.name == "Samat Pong" && scene.objects.size() == 3, "The Haerye sample scene parses.");
        require(scene.objects[0].shape == jm::HaeryeShape::Rectangle &&
                    scene.objects[2].shape == jm::HaeryeShape::Circle,
                "Haerye shape values parse.");

        const auto serialized = jm::serializeHaerye(scene);
        require(parseValid(serialized) == scene, "Haerye serialization round trip preserves the scene.");

        expectInvalid("scene \"x\" { object \"o\" { color: \"#GGGGGG\" } }", "non-hexadecimal");
        expectInvalid("scene \"x\" { object \"o\" { shape: rectangle } }", "needs shape");
        expectInvalid("scene \"x\" { object \"same\" { shape: rectangle position: (0, 0) size: (1, 1) "
                      "color: \"#FFFFFF\" } object \"same\" { shape: circle position: (0, 0) size: (1, 1) "
                      "color: \"#000000\" } }",
                      "Duplicate object name");

        std::cout << "Haerye standalone parser, validation, and serialization passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Haerye regression failed: " << error.what() << '\n';
        return 1;
    }
}
