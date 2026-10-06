#include "JMEngine/Core/Application.hpp"

#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char **argv) {
    try {
        std::size_t frames = 0;
        std::string script;
        for (int i = 1; i < argc; ++i) {
            auto arg = std::string(argv[i]);
            if ((arg != "--smoke-frames" && arg != "--smoke-language") || ++i >= argc)
                throw std::runtime_error(
                    "Usage: jmengine_sandbox [--smoke-frames count] [--smoke-language file]");
            if (arg == "--smoke-frames") {
                frames = std::stoull(argv[i]);
                if (!frames || frames > 10000)
                    throw std::runtime_error("Smoke frame count must be 1..10000.");
            } else
                script = argv[i];
        }
        jm::Application app({"JOSAMOSA ENGINE | Samat v0.5+", 1280, 720});
        if (!script.empty()) {
            std::ifstream input(script);
            if (!input)
                throw std::runtime_error("Cannot open smoke script.");
            std::string source(std::istreambuf_iterator<char>(input), {});
            app.startLanguagePlay(std::move(source), script.ends_with(".jmk"));
        }
        const auto rendered = app.run(frames);
        if (!script.empty()) {
            if (!app.isPlaying())
                throw std::runtime_error("Language Play stopped during smoke.");
            app.stopPlay();
            if (app.isPlaying())
                throw std::runtime_error("Language Stop failed.");
            std::cout << "Language Play/Stop smoke passed.\n";
        }
        if (frames) {
            if (rendered != frames)
                throw std::runtime_error("Sandbox stopped before rendering all requested frames.");
            std::cout << "Sandbox smoke: " << rendered << " SDL/OpenGL/ImGui frames rendered.\n";
        }
    } catch (const std::exception &exception) {
        std::cerr << "JM Engine failed to start: " << exception.what() << '\n';
        return 1;
    }

    return 0;
}
