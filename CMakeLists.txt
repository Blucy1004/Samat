cmake_minimum_required(VERSION 3.24)
project(JMEngine VERSION 0.6.0 LANGUAGES C CXX)

option(JMENGINE_BUILD_ENGINE "Build the SDL/OpenGL engine and editor" ON)
option(JMENGINE_BUILD_SANDBOX "Build the JM Engine sandbox application" ON)
option(JMENGINE_ENABLE_LLVM "Enable optional LLVM JIT and AOT backend" ON)
option(JMENGINE_BUILD_BENCHMARKS "Build small compiler correctness benchmark" OFF)
option(Samat_ENABLE_SANITIZERS "Instrument core/runtime and consumers with ASan/UBSan" OFF)
include(CTest)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(Samat_runtime STATIC src/JMEngine/Script/RuntimeABI.cpp)
set_target_properties(Samat_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(Samat_runtime PUBLIC cxx_std_20)

add_library(Samat_core
    src/JMEngine/Script/LanguageCore.cpp
    src/JMEngine/Script/ModuleLoader.cpp
    src/JMEngine/Script/TypeChecker.cpp
    src/JMEngine/Script/StandardLibrary.cpp
    src/JMEngine/Script/SurfaceRenderer.cpp
    src/JMEngine/Script/JMIR.cpp
    src/JMEngine/Script/IRVerifier.cpp
    src/JMEngine/Script/LLVMBackend.cpp
    src/JMEngine/Script/X64Backend.cpp
    src/JMEngine/Script/KoreanParticles.cpp
)
add_library(Samat::Core ALIAS Samat_core)
target_link_libraries(Samat_core PRIVATE Samat_runtime)
if(Samat_ENABLE_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "Combined ASan/UBSan configuration requires GCC or Clang")
    endif()
    foreach(JM_SANITIZED_TARGET Samat_core Samat_runtime)
        target_compile_options(${JM_SANITIZED_TARGET} PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${JM_SANITIZED_TARGET} PUBLIC -fsanitize=address,undefined)
    endforeach()
endif()
target_include_directories(Samat_core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
target_compile_features(Samat_core PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(Samat_core PRIVATE /W4 /permissive- /EHsc /utf-8)
else()
    target_compile_options(Samat_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(JMENGINE_ENABLE_LLVM)
    find_package(LLVM CONFIG QUIET)
    if(LLVM_FOUND AND LLVM_PACKAGE_VERSION VERSION_LESS 17)
        set(LLVM_FOUND FALSE)
    endif()
endif()
if(JMENGINE_ENABLE_LLVM AND LLVM_FOUND)
    message(STATUS "Samat LLVM backend: ${LLVM_PACKAGE_VERSION}")
    target_include_directories(Samat_core SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
    target_compile_definitions(Samat_core PUBLIC JMENGINE_HAS_LLVM=1)
    if(TARGET LLVM)
        target_link_libraries(Samat_core PRIVATE LLVM)
    else()
        llvm_map_components_to_libnames(JM_LLVM_LIBS core orcjit native nativecodegen passes target all)
        target_link_libraries(Samat_core PRIVATE ${JM_LLVM_LIBS})
    endif()
else()
    message(STATUS "Samat LLVM backend disabled/unavailable; Interpreter and bootstrap x64 enabled")
    target_compile_definitions(Samat_core PUBLIC JMENGINE_HAS_LLVM=0)
endif()
find_program(JM_CLANG NAMES clang clang-19 clang-18 clang-17)
find_program(JM_LLD_LINK NAMES lld-link lld-link-19 lld-link-18 lld-link-17)
if(NOT JM_CLANG)
    set(JM_CLANG clang)
endif()
if(NOT JM_LLD_LINK)
    set(JM_LLD_LINK lld-link)
endif()
target_compile_definitions(Samat_core PRIVATE JMENGINE_CLANG="${JM_CLANG}" JMENGINE_LLD_LINK="${JM_LLD_LINK}")

add_executable(Samat_cli src/ScriptCli/Main.cpp)
set_target_properties(Samat_cli PROPERTIES OUTPUT_NAME SamatCompiler)
target_link_libraries(Samat_cli PRIVATE Samat::Core)
if(MSVC)
    target_compile_options(Samat_cli PRIVATE /EHsc /utf-8)
endif()

if(JMENGINE_BUILD_ENGINE)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.10
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(SDL3)
    find_package(OpenGL REQUIRED)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.9b
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(imgui)
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    set(JSON_Install OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(nlohmann_json
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
        src/JMEngine/Script/EngineScriptAPI.cpp
        src/JMEngine/Scene/Scene.cpp
    )
    add_library(JMEngine::JMEngine ALIAS jmengine)
    target_include_directories(jmengine PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
    target_link_libraries(jmengine PUBLIC Samat::Core SDL3::SDL3 OpenGL::GL jmengine_imgui PRIVATE nlohmann_json::nlohmann_json)
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
    if(BUILD_TESTING)
        add_executable(jmengine_smoke tests/EngineSmoke.cpp)
        target_link_libraries(jmengine_smoke PRIVATE JMEngine::JMEngine)
        if(MSVC)
            target_compile_options(jmengine_smoke PRIVATE /EHsc /utf-8)
        endif()
        add_test(NAME jmengine_smoke COMMAND jmengine_smoke)
    endif()
endif()

if(BUILD_TESTING)
    add_executable(Samat_compiler_tests tests/CompilerRegression.cpp)
    if(JMENGINE_BUILD_ENGINE)
        target_link_libraries(Samat_compiler_tests PRIVATE JMEngine::JMEngine)
        target_compile_definitions(Samat_compiler_tests PRIVATE JMENGINE_HAS_ENGINE=1)
    else()
        target_link_libraries(Samat_compiler_tests PRIVATE Samat::Core)
        target_compile_definitions(Samat_compiler_tests PRIVATE JMENGINE_HAS_ENGINE=0)
    endif()
    if(MSVC)
        target_compile_options(Samat_compiler_tests PRIVATE /EHsc /utf-8)
    endif()
    add_test(NAME Samat_compiler_regression COMMAND Samat_compiler_tests)
    add_executable(Samat_extended_tests tests/ExtendedRegression.cpp)
    if(JMENGINE_BUILD_ENGINE)
        target_link_libraries(Samat_extended_tests PRIVATE JMEngine::JMEngine)
        target_compile_definitions(Samat_extended_tests PRIVATE JMENGINE_HAS_ENGINE=1)
    else()
        target_link_libraries(Samat_extended_tests PRIVATE Samat::Core)
        target_compile_definitions(Samat_extended_tests PRIVATE JMENGINE_HAS_ENGINE=0)
    endif()
    add_test(NAME Samat_extended_regression COMMAND Samat_extended_tests)
    add_executable(Samat_native_data_tests tests/NativeDataRegression.cpp)
    if(JMENGINE_BUILD_ENGINE)
        target_link_libraries(Samat_native_data_tests PRIVATE JMEngine::JMEngine)
        target_compile_definitions(Samat_native_data_tests PRIVATE JMENGINE_HAS_ENGINE=1)
    else()
        target_link_libraries(Samat_native_data_tests PRIVATE Samat::Core)
        target_compile_definitions(Samat_native_data_tests PRIVATE JMENGINE_HAS_ENGINE=0)
    endif()
    add_test(NAME Samat_native_data_regression COMMAND Samat_native_data_tests)
    add_executable(Samat_runtime_memory tests/RuntimeMemory.cpp)
    target_link_libraries(Samat_runtime_memory PRIVATE Samat::Core)
    if(MSVC)
        target_compile_options(Samat_runtime_memory PRIVATE /EHsc /utf-8)
    endif()
    add_test(NAME Samat_runtime_memory COMMAND Samat_runtime_memory)
endif()
if(JMENGINE_BUILD_BENCHMARKS)
    add_executable(Samat_benchmark tests/CompilerBenchmark.cpp)
    target_link_libraries(Samat_benchmark PRIVATE Samat::Core)
endif()

# The AOT runtime archive is relocatable beside the installed CLI.
install(TARGETS Samat_cli Samat_runtime
    RUNTIME DESTINATION bin COMPONENT Samat
    ARCHIVE DESTINATION bin COMPONENT Samat)
