#include "JMEngine/Renderer/GLApi.hpp"

#include <SDL3/SDL_video.h>

namespace jm::gl {

PFNGLCREATESHADERPROC CreateShader = nullptr;
PFNGLSHADERSOURCEPROC ShaderSource = nullptr;
PFNGLCOMPILESHADERPROC CompileShader = nullptr;
PFNGLGETSHADERIVPROC GetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog = nullptr;
PFNGLDELETESHADERPROC DeleteShader = nullptr;
PFNGLCREATEPROGRAMPROC CreateProgram = nullptr;
PFNGLATTACHSHADERPROC AttachShader = nullptr;
PFNGLLINKPROGRAMPROC LinkProgram = nullptr;
PFNGLGETPROGRAMIVPROC GetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog = nullptr;
PFNGLDELETEPROGRAMPROC DeleteProgram = nullptr;
PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation = nullptr;
PFNGLUSEPROGRAMPROC UseProgram = nullptr;
PFNGLUNIFORMMATRIX4FVPROC UniformMatrix4fv = nullptr;
PFNGLUNIFORM1IPROC Uniform1i = nullptr;
PFNGLUNIFORM3FVPROC Uniform3fv = nullptr;
PFNGLGENVERTEXARRAYSPROC GenVertexArrays = nullptr;
PFNGLBINDVERTEXARRAYPROC BindVertexArray = nullptr;
PFNGLGENBUFFERSPROC GenBuffers = nullptr;
PFNGLBINDBUFFERPROC BindBuffer = nullptr;
PFNGLBUFFERDATAPROC BufferData = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray = nullptr;
PFNGLDELETEBUFFERSPROC DeleteBuffers = nullptr;
PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays = nullptr;

bool load() {
    bool loaded = true;
#define JM_LOAD_GL(type, name, symbol) \
    name = reinterpret_cast<type>(SDL_GL_GetProcAddress(symbol)); \
    loaded = (name != nullptr) && loaded;

    JM_LOAD_GL(PFNGLCREATESHADERPROC, CreateShader, "glCreateShader")
    JM_LOAD_GL(PFNGLSHADERSOURCEPROC, ShaderSource, "glShaderSource")
    JM_LOAD_GL(PFNGLCOMPILESHADERPROC, CompileShader, "glCompileShader")
    JM_LOAD_GL(PFNGLGETSHADERIVPROC, GetShaderiv, "glGetShaderiv")
    JM_LOAD_GL(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog, "glGetShaderInfoLog")
    JM_LOAD_GL(PFNGLDELETESHADERPROC, DeleteShader, "glDeleteShader")
    JM_LOAD_GL(PFNGLCREATEPROGRAMPROC, CreateProgram, "glCreateProgram")
    JM_LOAD_GL(PFNGLATTACHSHADERPROC, AttachShader, "glAttachShader")
    JM_LOAD_GL(PFNGLLINKPROGRAMPROC, LinkProgram, "glLinkProgram")
    JM_LOAD_GL(PFNGLGETPROGRAMIVPROC, GetProgramiv, "glGetProgramiv")
    JM_LOAD_GL(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog, "glGetProgramInfoLog")
    JM_LOAD_GL(PFNGLDELETEPROGRAMPROC, DeleteProgram, "glDeleteProgram")
    JM_LOAD_GL(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation, "glGetUniformLocation")
    JM_LOAD_GL(PFNGLUSEPROGRAMPROC, UseProgram, "glUseProgram")
    JM_LOAD_GL(PFNGLUNIFORMMATRIX4FVPROC, UniformMatrix4fv, "glUniformMatrix4fv")
    JM_LOAD_GL(PFNGLUNIFORM1IPROC, Uniform1i, "glUniform1i")
    JM_LOAD_GL(PFNGLUNIFORM3FVPROC, Uniform3fv, "glUniform3fv")
    JM_LOAD_GL(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays, "glGenVertexArrays")
    JM_LOAD_GL(PFNGLBINDVERTEXARRAYPROC, BindVertexArray, "glBindVertexArray")
    JM_LOAD_GL(PFNGLGENBUFFERSPROC, GenBuffers, "glGenBuffers")
    JM_LOAD_GL(PFNGLBINDBUFFERPROC, BindBuffer, "glBindBuffer")
    JM_LOAD_GL(PFNGLBUFFERDATAPROC, BufferData, "glBufferData")
    JM_LOAD_GL(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer, "glVertexAttribPointer")
    JM_LOAD_GL(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray, "glEnableVertexAttribArray")
    JM_LOAD_GL(PFNGLDELETEBUFFERSPROC, DeleteBuffers, "glDeleteBuffers")
    JM_LOAD_GL(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays, "glDeleteVertexArrays")

#undef JM_LOAD_GL
    return loaded;
}

} // namespace jm::gl
