cmake_minimum_required(VERSION 3.24)

project(JMEngine VERSION 0.1.0 LANGUAGES C CXX)

option(JMENGINE_BUILD_SANDBOX "Build the JM Engine sandbox application" ON)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.10
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(SDL3)
find_package(OpenGL REQUIRED)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(jmengine_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(jmengine_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(jmengine_imgui PUBLIC SDL3::SDL3 OpenGL::GL)

add_library(jmengine
    src/JMEngine/Core/Application.cpp
    src/JMEngine/Renderer/GLApi.cpp
    src/JMEngine/Renderer/Renderer.cpp
    src/JMEngine/Project/ProjectStore.cpp
    src/JMEngine/Script/Script.cpp
    src/JMEngine/Script/LanguageCore.cpp
    src/JMEngine/Script/JMIR.cpp
    src/JMEngine/Script/X64Backend.cpp
    src/JMEngine/Script/KoreanParticles.cpp
    src/JMEngine/Scene/Scene.cpp
)
add_library(JMEngine::JMEngine ALIAS jmengine)

target_include_directories(jmengine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_link_libraries(jmengine PUBLIC SDL3::SDL3 OpenGL::GL jmengine_imgui PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(jmengine PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(jmengine PRIVATE /W4 /permissive- /EHsc /utf-8)
else()
    target_compile_options(jmengine PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(JMENGINE_BUILD_SANDBOX)
    add_executable(jmengine_sandbox src/Sandbox/Main.cpp)
    target_link_libraries(jmengine_sandbox PRIVATE JMEngine::JMEngine)
    if(MSVC)
        target_compile_options(jmengine_sandbox PRIVATE /EHsc /utf-8)
    endif()
endif()

add_executable(Samat_cli src/ScriptCli/Main.cpp)
set_target_properties(Samat_cli PROPERTIES OUTPUT_NAME SamatCompiler)
target_link_libraries(Samat_cli PRIVATE JMEngine::JMEngine)
if(MSVC)
    target_compile_options(Samat_cli PRIVATE /EHsc /utf-8)
endif()

if(BUILD_TESTING)
    add_executable(jmengine_smoke tests/EngineSmoke.cpp)
    target_link_libraries(jmengine_smoke PRIVATE JMEngine::JMEngine)
    if(MSVC)
        target_compile_options(jmengine_smoke PRIVATE /EHsc /utf-8)
    endif()
    add_test(NAME jmengine_smoke COMMAND jmengine_smoke)
endif()
