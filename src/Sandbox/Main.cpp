#include "JMEngine/Core/Application.hpp"

#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>

int main(int argc, char **argv) {
    try {
        std::size_t frames = 0, cycles = 1;
        std::string script, liveScript, haeryeFile, languageFile;
        auto backend = jm::EngineScriptBackend::Interpreter;
        for (int i = 1; i < argc; ++i) {
            auto arg = std::string(argv[i]);
            if ((arg != "--smoke-frames" && arg != "--smoke-language" && arg != "--smoke-cycles" &&
                 arg != "--smoke-live" && arg != "--smoke-backend" && arg != "--haerye" &&
                 arg != "--language") ||
                ++i >= argc)
                throw std::runtime_error("Usage: SamatStudio [--smoke-frames count] [--smoke-language "
                                         "file] [--smoke-cycles count] [--smoke-live candidate] "
                                         "[--haerye scene.hy] [--language logic.st]");
            if (arg == "--smoke-frames") {
                frames = std::stoull(argv[i]);
                if (!frames || frames > 10000)
                    throw std::runtime_error("Smoke frame count must be 1..10000.");
            } else if (arg == "--smoke-cycles") {
                cycles = std::stoull(argv[i]);
                if (!cycles || cycles > 1000)
                    throw std::runtime_error("Smoke cycles must be 1..1000.");
            } else if (arg == "--smoke-backend") {
                auto value = std::string(argv[i]);
                if (value != "interpreter" && value != "llvm")
                    throw std::runtime_error("Smoke backend must be interpreter or llvm.");
                backend =
                    value == "llvm" ? jm::EngineScriptBackend::LLVM : jm::EngineScriptBackend::Interpreter;
            } else if (arg == "--smoke-live")
                liveScript = argv[i];
            else if (arg == "--haerye")
                haeryeFile = argv[i];
            else if (arg == "--language")
                languageFile = argv[i];
            else
                script = argv[i];
        }
        if (!haeryeFile.empty() && !haeryeFile.ends_with(".hy"))
            throw std::runtime_error("Haerye scenes must use the canonical .hy extension.");
        if (!languageFile.empty() && !languageFile.ends_with(".st"))
            throw std::runtime_error("Samat source must use the canonical .st extension.");
        if (!languageFile.empty() && !script.empty())
            throw std::runtime_error("Choose either --language or --smoke-language, not both.");
        if (!languageFile.empty() && cycles != 1)
            throw std::runtime_error("--language can be started only once per sandbox process.");
        if (cycles > 1 && (script.empty() || !frames))
            throw std::runtime_error("Repeated Play smoke requires a script and bounded frames.");
        jm::Application app({"Samat Studio", 1280, 720});
        if (!haeryeFile.empty()) {
            std::ifstream input(haeryeFile, std::ios::binary);
            if (!input)
                throw std::runtime_error("Cannot open Haerye scene.");
            std::string source(std::istreambuf_iterator<char>(input), {});
            app.loadHaerye(std::move(source));
        }
        if (!languageFile.empty()) {
            std::ifstream input(languageFile, std::ios::binary);
            if (!input)
                throw std::runtime_error("Cannot open Samat source.");
            std::string source(std::istreambuf_iterator<char>(input), {});
            app.startLanguagePlay(std::move(source), false, backend);
        }
        std::string source;
        if (!script.empty()) {
            std::ifstream input(script);
            if (!input)
                throw std::runtime_error("Cannot open smoke script.");
            source.assign(std::istreambuf_iterator<char>(input), {});
        }
        std::string candidate;
        if (!liveScript.empty()) {
            std::ifstream input(liveScript);
            if (!input)
                throw std::runtime_error("Cannot open live smoke candidate.");
            candidate.assign(std::istreambuf_iterator<char>(input), {});
        }
        const auto snapshot = app.scene().objects();
        std::size_t renderedTotal = 0;
        for (std::size_t cycle = 0; cycle < cycles; ++cycle) {
            if (!script.empty())
                app.startLanguagePlay(source, script.ends_with(".jmk"), backend);
            if (!liveScript.empty()) {
                if (!app.applyLanguageEdit(candidate, liveScript.ends_with(".jmk")))
                    throw std::runtime_error("Valid live smoke edit rejected.");
                if (app.applyLanguageEdit("fn :", false))
                    throw std::runtime_error("Broken live edit was accepted.");
                if (!app.isPlaying())
                    throw std::runtime_error("Broken edit stopped Play.");
            }
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
        if (!languageFile.empty()) {
            app.stopPlay();
            if (app.scene().objects().size() != snapshot.size())
                throw std::runtime_error("Stop failed to restore Haerye Scene object count.");
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
