#include "JMEngine/Renderer/Renderer.hpp"
#include "JMEngine/Renderer/Viewport.hpp"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

namespace jm {
namespace {

constexpr const char* vertexShaderSource = R"(#version 330 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aColor;
layout (location = 2) in vec3 aNormal;
uniform mat4 uMVP;
uniform mat4 uModel;
uniform vec3 uTint;
out vec3 vertexColor;
out vec3 surfaceNormal;
void main() {
    gl_Position = uMVP * vec4(aPosition, 1.0);
    vertexColor = aColor * uTint;
    surfaceNormal = mat3(transpose(inverse(uModel))) * aNormal;
}
)";

constexpr const char* fragmentShaderSource = R"(#version 330 core
in vec3 vertexColor;
in vec3 surfaceNormal;
uniform int uUseLighting;
out vec4 FragColor;
void main() {
    float light = 1.0;
    if (uUseLighting != 0) {
        vec3 normal = normalize(surfaceNormal);
        vec3 keyLight = normalize(vec3(-0.45, 0.78, 0.55));
        vec3 fillLight = normalize(vec3(0.65, 0.25, -0.72));
        light = 0.22 + 0.63 * max(dot(normal, keyLight), 0.0)
                     + 0.15 * max(dot(normal, fillLight), 0.0);
    }
    FragColor = vec4(vertexColor * light, 1.0);
}
)";

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = gl::CreateShader(type);
    gl::ShaderSource(shader, 1, &source, nullptr);
    gl::CompileShader(shader);

    GLint success = GL_FALSE;
    gl::GetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_TRUE) return shader;

    GLint logLength = 0;
    gl::GetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(logLength), '\0');
    gl::GetShaderInfoLog(shader, logLength, nullptr, log.data());
    gl::DeleteShader(shader);
    throw std::runtime_error("OpenGL shader compilation failed: " + log);
}

GLuint createProgram() {
    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = 0;
    GLuint program = 0;
    try {
        fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
        program = gl::CreateProgram();
        gl::AttachShader(program, vertexShader);
        gl::AttachShader(program, fragmentShader);
        gl::LinkProgram(program);

        GLint success = GL_FALSE;
        gl::GetProgramiv(program, GL_LINK_STATUS, &success);
        if (success != GL_TRUE) {
            GLint logLength = 0;
            gl::GetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
            std::string log(static_cast<std::size_t>(logLength), '\0');
            gl::GetProgramInfoLog(program, logLength, nullptr, log.data());
            throw std::runtime_error("OpenGL shader link failed: " + log);
        }
    } catch (...) {
        if (program != 0) gl::DeleteProgram(program);
        if (fragmentShader != 0) gl::DeleteShader(fragmentShader);
        gl::DeleteShader(vertexShader);
        throw;
    }

    gl::DeleteShader(vertexShader);
    gl::DeleteShader(fragmentShader);
    return program;
}

void appendFace(std::vector<Vertex>& vertices, const std::array<float, 3>& a,
                const std::array<float, 3>& b, const std::array<float, 3>& c,
                const std::array<float, 3>& d, const std::array<float, 3>& color,
                const std::array<float, 3>& normal) {
    for (const auto& point : {a, b, c, a, c, d}) {
        vertices.push_back({{point[0], point[1], point[2]}, {color[0], color[1], color[2]},
                            {normal[0], normal[1], normal[2]}});
    }
}

std::vector<Vertex> makeCubeVertices() {
    std::vector<Vertex> vertices;
    vertices.reserve(36);
    appendFace(vertices, {-0.5F,-0.5F, 0.5F}, { 0.5F,-0.5F, 0.5F}, { 0.5F, 0.5F, 0.5F}, {-0.5F, 0.5F, 0.5F}, {0.95F,0.35F,0.28F}, { 0.0F, 0.0F, 1.0F});
    appendFace(vertices, { 0.5F,-0.5F,-0.5F}, {-0.5F,-0.5F,-0.5F}, {-0.5F, 0.5F,-0.5F}, { 0.5F, 0.5F,-0.5F}, {0.25F,0.70F,0.95F}, { 0.0F, 0.0F,-1.0F});
    appendFace(vertices, {-0.5F, 0.5F, 0.5F}, { 0.5F, 0.5F, 0.5F}, { 0.5F, 0.5F,-0.5F}, {-0.5F, 0.5F,-0.5F}, {0.95F,0.72F,0.30F}, { 0.0F, 1.0F, 0.0F});
    appendFace(vertices, {-0.5F,-0.5F,-0.5F}, { 0.5F,-0.5F,-0.5F}, { 0.5F,-0.5F, 0.5F}, {-0.5F,-0.5F, 0.5F}, {0.45F,0.80F,0.50F}, { 0.0F,-1.0F, 0.0F});
    appendFace(vertices, { 0.5F,-0.5F, 0.5F}, { 0.5F,-0.5F,-0.5F}, { 0.5F, 0.5F,-0.5F}, { 0.5F, 0.5F, 0.5F}, {0.75F,0.45F,0.88F}, { 1.0F, 0.0F, 0.0F});
    appendFace(vertices, {-0.5F,-0.5F,-0.5F}, {-0.5F,-0.5F, 0.5F}, {-0.5F, 0.5F, 0.5F}, {-0.5F, 0.5F,-0.5F}, {0.30F,0.78F,0.78F}, {-1.0F, 0.0F, 0.0F});
    return vertices;
}

std::vector<Vertex> makeGridVertices() {
    std::vector<Vertex> vertices;
    vertices.reserve(84);
    for (int coordinate = -10; coordinate <= 10; ++coordinate) {
        const float position = static_cast<float>(coordinate);
        const auto color = coordinate == 0
            ? std::array<float, 3>{0.30F, 0.78F, 0.92F}
            : std::array<float, 3>{0.17F, 0.23F, 0.34F};
        const std::array<float, 3> normal{0.0F, 0.0F, 1.0F};
        vertices.push_back({{-10.0F, position, 0.0F}, {color[0], color[1], color[2]},
                            {normal[0], normal[1], normal[2]}});
        vertices.push_back({{10.0F, position, 0.0F}, {color[0], color[1], color[2]},
                            {normal[0], normal[1], normal[2]}});
        vertices.push_back({{position, -10.0F, 0.0F}, {color[0], color[1], color[2]},
                            {normal[0], normal[1], normal[2]}});
        vertices.push_back({{position, 10.0F, 0.0F}, {color[0], color[1], color[2]},
                            {normal[0], normal[1], normal[2]}});
    }
    return vertices;
}

std::vector<Vertex> makePanelVertices() {
    constexpr float normal[3]{0.0F, 0.0F, 1.0F};
    return {
        {{-0.5F,-0.5F,0.0F}, {0.20F,0.80F,0.95F}, {normal[0],normal[1],normal[2]}},
        {{ 0.5F,-0.5F,0.0F}, {0.34F,0.45F,0.95F}, {normal[0],normal[1],normal[2]}},
        {{ 0.5F, 0.5F,0.0F}, {0.95F,0.42F,0.35F}, {normal[0],normal[1],normal[2]}},
        {{-0.5F,-0.5F,0.0F}, {0.20F,0.80F,0.95F}, {normal[0],normal[1],normal[2]}},
        {{ 0.5F, 0.5F,0.0F}, {0.95F,0.42F,0.35F}, {normal[0],normal[1],normal[2]}},
        {{-0.5F, 0.5F,0.0F}, {0.95F,0.68F,0.30F}, {normal[0],normal[1],normal[2]}},
    };
}

} // namespace

Renderer::Renderer() {
    try {
        program_ = createProgram();
        mvpLocation_ = gl::GetUniformLocation(program_, "uMVP");
        modelLocation_ = gl::GetUniformLocation(program_, "uModel");
        lightingLocation_ = gl::GetUniformLocation(program_, "uUseLighting");
        tintLocation_ = gl::GetUniformLocation(program_, "uTint");
        cube_ = createMesh(makeCubeVertices());
        grid_ = createMesh(makeGridVertices());
        panel_ = createMesh(makePanelVertices());
        glEnable(GL_DEPTH_TEST);
    } catch (...) {
        destroy();
        throw;
    }
}

Renderer::~Renderer() {
    destroy();
}

Renderer::Mesh Renderer::createMesh(const std::vector<Vertex>& vertices) {
    Mesh mesh{};
    mesh.vertexCount = static_cast<GLsizei>(vertices.size());
    gl::GenVertexArrays(1, &mesh.vertexArray);
    gl::GenBuffers(1, &mesh.vertexBuffer);
    gl::BindVertexArray(mesh.vertexArray);
    gl::BindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
    gl::BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                   vertices.data(), GL_STATIC_DRAW);
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                            reinterpret_cast<const void*>(offsetof(Vertex, color)));
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                            reinterpret_cast<const void*>(offsetof(Vertex, normal)));
    gl::EnableVertexAttribArray(2);
    gl::BindVertexArray(0);
    return mesh;
}

void Renderer::drawMesh(const Mesh& mesh, const Matrix4& viewProjection, const Matrix4& model,
                        GLenum primitive, bool lighting, Vec3 tint) {
    const Matrix4 mvp = viewProjection * model;
    gl::UniformMatrix4fv(mvpLocation_, 1, GL_FALSE, mvp.values.data());
    gl::UniformMatrix4fv(modelLocation_, 1, GL_FALSE, model.values.data());
    gl::Uniform1i(lightingLocation_, lighting ? 1 : 0);
    gl::Uniform3fv(tintLocation_, 1, &tint.x);
    gl::BindVertexArray(mesh.vertexArray);
    glDrawArrays(primitive, 0, mesh.vertexCount);
}

void Renderer::drawScene(int framebufferWidth, int framebufferHeight, int viewportX, int viewportY,
                         int viewportWidth, int viewportHeight, bool twoDimensional, float cameraYaw,
                         float cameraPitch, float cameraDistance, float panX2D, float panY2D, float zoom2D,
                         const std::vector<GameObject>& objects) {
    if (framebufferWidth <= 0 || framebufferHeight <= 0 || viewportWidth <= 0 || viewportHeight <= 0) return;
    const PixelViewport viewport{viewportX, viewportY, viewportWidth, viewportHeight};
    const int openGlY = viewportBottomLeftY(viewport, framebufferHeight);
    glEnable(GL_SCISSOR_TEST);
    glScissor(viewportX, openGlY, viewportWidth, viewportHeight);
    glViewport(viewportX, openGlY, viewportWidth, viewportHeight);
    glClearColor(0.055F, 0.070F, 0.105F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    gl::UseProgram(program_);
    const float aspect = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
    if (twoDimensional) {
        glDisable(GL_DEPTH_TEST);
        const float halfHeight = 5.0F / zoom2D;
        const float halfWidth = halfHeight * aspect;
        const Matrix4 projection = Matrix4::orthographic(-halfWidth, halfWidth, -halfHeight, halfHeight);
        const Matrix4 camera = Matrix4::translation(-panX2D, -panY2D, 0.0F);
        drawMesh(grid_, projection, camera, GL_LINES, false, {1.0F, 1.0F, 1.0F});
        std::vector<const GameObject*> sprites;
        for (const GameObject& object : objects) {
            if (object.kind == ObjectKind::Sprite2D && object.visible) sprites.push_back(&object);
        }
        std::stable_sort(sprites.begin(), sprites.end(), [](const GameObject* left, const GameObject* right) {
            return left->layer < right->layer;
        });
        for (const GameObject* sprite : sprites) {
            const GameObject& object = *sprite;
            const float angle = object.rotationDegrees.z * 0.01745329252F;
            const Matrix4 model = camera * Matrix4::translation(object.position.x, object.position.y, 0.0F) *
                                  Matrix4::rotationZ(angle) * Matrix4::scale(object.scale.x, object.scale.y, 1.0F);
            drawMesh(panel_, projection, model, GL_TRIANGLES, false, object.color);
        }
        glEnable(GL_DEPTH_TEST);
    } else {
        glEnable(GL_DEPTH_TEST);
        const float horizontal = cameraDistance * std::cos(cameraPitch);
        const Vec3 eye{horizontal * std::sin(cameraYaw), cameraDistance * std::sin(cameraPitch),
                       horizontal * std::cos(cameraYaw)};
        const Matrix4 projection = Matrix4::perspective(0.785398F, aspect, 0.1F, 100.0F);
        const Matrix4 view = Matrix4::lookAt(eye, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
        constexpr float degreesToRadians = 0.01745329252F;
        const Matrix4 viewProjection = projection * view;
        for (const GameObject& object : objects) {
            if (object.kind != ObjectKind::Cube3D || !object.visible) continue;
            const Matrix4 model = Matrix4::translation(object.position.x, object.position.y, object.position.z) *
                                  Matrix4::rotationZ(object.rotationDegrees.z * degreesToRadians) *
                                  Matrix4::rotationY(object.rotationDegrees.y * degreesToRadians) *
                                  Matrix4::rotationX(object.rotationDegrees.x * degreesToRadians) *
                                  Matrix4::scale(object.scale.x, object.scale.y, object.scale.z);
            drawMesh(cube_, viewProjection, model, GL_TRIANGLES, true, object.color);
        }
    }
    gl::BindVertexArray(0);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
}

void Renderer::destroy() noexcept {
    for (Mesh* mesh : {&cube_, &grid_, &panel_}) {
        if (mesh->vertexBuffer != 0) gl::DeleteBuffers(1, &mesh->vertexBuffer);
        if (mesh->vertexArray != 0) gl::DeleteVertexArrays(1, &mesh->vertexArray);
        *mesh = {};
    }
    if (program_ != 0) {
        gl::DeleteProgram(program_);
        program_ = 0;
    }
}

} // namespace jm

