#include "JMEngine/Core/Application.hpp"

#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char **argv) {
    try {
        std::size_t frames = 0, cycles = 1;
        std::string script;
        for (int i = 1; i < argc; ++i) {
            auto arg = std::string(argv[i]);
            if ((arg != "--smoke-frames" && arg != "--smoke-language" && arg != "--smoke-cycles") ||
                ++i >= argc)
                throw std::runtime_error("Usage: jmengine_sandbox [--smoke-frames count] [--smoke-language "
                                         "file] [--smoke-cycles count]");
            if (arg == "--smoke-frames") {
                frames = std::stoull(argv[i]);
                if (!frames || frames > 10000)
                    throw std::runtime_error("Smoke frame count must be 1..10000.");
            } else if (arg == "--smoke-cycles") {
                cycles = std::stoull(argv[i]);
                if (!cycles || cycles > 1000)
                    throw std::runtime_error("Smoke cycles must be 1..1000.");
            } else
                script = argv[i];
        }
        if (cycles > 1 && (script.empty() || !frames))
            throw std::runtime_error("Repeated Play smoke requires a script and bounded frames.");
        jm::Application app({"JOSAMOSA ENGINE | Samat v0.6", 1280, 720});
        std::string source;
        if (!script.empty()) {
            std::ifstream input(script);
            if (!input)
                throw std::runtime_error("Cannot open smoke script.");
            source.assign(std::istreambuf_iterator<char>(input), {});
        }
        const auto snapshot = app.scene().objects();
        std::size_t renderedTotal = 0;
        for (std::size_t cycle = 0; cycle < cycles; ++cycle) {
            if (!script.empty())
                app.startLanguagePlay(source, script.ends_with(".jmk"));
            auto rendered = app.run(frames);
            renderedTotal += rendered;
            if (frames && rendered != frames)
                throw std::runtime_error("Sandbox stopped before rendering requested frames.");
            if (!script.empty()) {
                if (!app.isPlaying())
                    throw std::runtime_error("Language Play stopped during smoke.");
                app.stopPlay();
                if (app.isPlaying())
                    throw std::runtime_error("Language Stop failed.");
                auto &objects = app.scene().objects();
                if (objects.size() != snapshot.size())
                    throw std::runtime_error("Stop failed to restore Scene object count.");
                for (size_t i = 0; i < objects.size(); ++i) {
                    auto &a = objects[i];
                    auto &b = snapshot[i];
                    if (a.id != b.id || a.position.x != b.position.x || a.position.y != b.position.y ||
                        a.position.z != b.position.z || a.horizontalVelocity != b.horizontalVelocity ||
                        a.verticalVelocity != b.verticalVelocity)
                        throw std::runtime_error("Stop failed to restore Scene state.");
                }
            }
        }
        if (!script.empty())
            std::cout << "Language Play/Stop smoke passed: " << cycles << " cycles, Scene state restored.\n";
        if (frames)
            std::cout << "Sandbox smoke: " << renderedTotal << " SDL/OpenGL/ImGui frames rendered.\n";
    } catch (const std::exception &exception) {
        std::cerr << "JM Engine failed to start: " << exception.what() << '\n';
        return 1;
    }

    return 0;
}
