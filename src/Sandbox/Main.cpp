#include "JMEngine/Core/Application.hpp"

#include <exception>
#include <iostream>

int main(int argc,char** argv) {
    try {
        std::size_t frames=0;
        if(argc!=1) {
            if(argc!=3 || std::string(argv[1])!="--smoke-frames")throw std::runtime_error("Usage: jmengine_sandbox [--smoke-frames count]");
            frames=std::stoull(argv[2]);if(frames==0 || frames>10000)throw std::runtime_error("Smoke frame count must be in 1..10000.");
        }
        jm::Application app({"JOSAMOSA ENGINE | Samat v0.5", 1280, 720});
        const auto rendered=app.run(frames);
        if(frames) {
            if(rendered!=frames)throw std::runtime_error("Sandbox stopped before rendering all requested frames.");
            std::cout<<"Sandbox smoke: "<<rendered<<" SDL/OpenGL/ImGui frames rendered.\n";
        }
    } catch (const std::exception& exception) {
        std::cerr << "JM Engine failed to start: " << exception.what() << '\n';
        return 1;
    }

    return 0;
}
