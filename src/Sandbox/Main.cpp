#include "JMEngine/Core/Application.hpp"

#include <exception>
#include <iostream>

int main() {
    try {
        jm::Application app({"JOSAMOSA ENGINE | JM Engine v0.1", 1280, 720});
        app.run();
    } catch (const std::exception& exception) {
        std::cerr << "JM Engine failed to start: " << exception.what() << '\n';
        return 1;
    }

    return 0;
}
