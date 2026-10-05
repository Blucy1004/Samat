#pragma once

#include "JMEngine/Renderer/GLApi.hpp"
#include "JMEngine/Renderer/Matrix4.hpp"
#include "JMEngine/Scene/Scene.hpp"

#include <vector>

namespace jm {

struct Vertex {
    float position[3];
    float color[3];
    float normal[3];
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void drawScene(int framebufferWidth, int framebufferHeight, int viewportX, int viewportY,
                   int viewportWidth, int viewportHeight, bool twoDimensional, float cameraYaw,
                   float cameraPitch, float cameraDistance, float panX2D, float panY2D, float zoom2D,
                   const std::vector<GameObject>& objects);

private:
    struct Mesh {
        GLuint vertexArray{0};
        GLuint vertexBuffer{0};
        GLsizei vertexCount{0};
    };

    Mesh createMesh(const std::vector<Vertex>& vertices);
    void drawMesh(const Mesh& mesh, const Matrix4& viewProjection, const Matrix4& model,
                  GLenum primitive, bool lighting, Vec3 tint);
    void destroy() noexcept;

    GLuint program_{0};
    GLint mvpLocation_{-1};
    GLint modelLocation_{-1};
    GLint lightingLocation_{-1};
    GLint tintLocation_{-1};
    Mesh cube_{};
    Mesh grid_{};
    Mesh panel_{};
};

} // namespace jm
