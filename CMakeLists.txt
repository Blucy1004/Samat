cmake_minimum_required(VERSION 3.24)
project(Samat VERSION 1.0.0 LANGUAGES C CXX)

option(SAMAT_ENABLE_LLVM "Enable optional LLVM JIT and AOT backend" ON)
option(SAMAT_BUILD_BENCHMARKS "Build the compiler correctness benchmark" OFF)
option(SAMAT_ENABLE_SANITIZERS "Instrument Samat libraries and consumers with ASan/UBSan" OFF)

include(CTest)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
endif()

add_library(samat_runtime STATIC src/Samat/Language/RuntimeABI.cpp)
set_target_properties(samat_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(samat_runtime PUBLIC cxx_std_20)

add_library(samat_core
    src/Samat/Language/LanguageCore.cpp
    src/Samat/EditorSupport.cpp
    src/Samat/Haerye.cpp
    src/Samat/Language/ModuleLoader.cpp
    src/Samat/Language/TypeChecker.cpp
    src/Samat/Language/StandardLibrary.cpp
    src/Samat/Language/SurfaceRenderer.cpp
    src/Samat/Language/LanguageMetadata.cpp
    src/Samat/Language/JMIR.cpp
    src/Samat/Language/IRVerifier.cpp
    src/Samat/Language/LLVMBackend.cpp
    src/Samat/Language/X64Backend.cpp
    src/Samat/Language/KoreanParticles.cpp
)
add_library(Samat::Core ALIAS samat_core)
target_link_libraries(samat_core PRIVATE samat_runtime)
target_include_directories(samat_core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
target_compile_features(samat_core PUBLIC cxx_std_20)
if(WIN32)
    target_link_libraries(samat_core PRIVATE comdlg32)
endif()

if(SAMAT_ENABLE_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "The combined ASan/UBSan configuration requires GCC or Clang.")
    endif()
    foreach(target samat_core samat_runtime)
        target_compile_options(${target} PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PUBLIC -fsanitize=address,undefined)
    endforeach()
endif()

if(MSVC)
    target_compile_options(samat_core PRIVATE /W4 /permissive- /EHs /utf-8)
    target_compile_options(samat_runtime PRIVATE /EHs /utf-8)
else()
    target_compile_options(samat_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SAMAT_ENABLE_LLVM)
    find_package(LLVM CONFIG QUIET)
    if(LLVM_FOUND AND LLVM_PACKAGE_VERSION VERSION_LESS 17)
        set(LLVM_FOUND FALSE)
    endif()
endif()
if(SAMAT_ENABLE_LLVM AND LLVM_FOUND)
    message(STATUS "Samat LLVM backend: ${LLVM_PACKAGE_VERSION}")
    target_include_directories(samat_core SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
    target_compile_definitions(samat_core PUBLIC SAMAT_HAS_LLVM=1)
    if(TARGET LLVM)
        target_link_libraries(samat_core PRIVATE LLVM)
    else()
        llvm_map_components_to_libnames(SAMAT_LLVM_LIBS core orcjit native nativecodegen passes target all)
        target_link_libraries(samat_core PRIVATE ${SAMAT_LLVM_LIBS})
    endif()
else()
    message(STATUS "Samat LLVM backend disabled/unavailable; Interpreter and bootstrap x64 enabled")
    target_compile_definitions(samat_core PUBLIC SAMAT_HAS_LLVM=0)
endif()

find_program(SAMAT_CLANG NAMES clang clang-19 clang-18 clang-17)
find_program(SAMAT_LLD_LINK NAMES lld-link lld-link-19 lld-link-18 lld-link-17)
if(NOT SAMAT_CLANG)
    set(SAMAT_CLANG clang)
endif()
if(NOT SAMAT_LLD_LINK)
    set(SAMAT_LLD_LINK lld-link)
endif()
target_compile_definitions(samat_core PRIVATE SAMAT_CLANG="${SAMAT_CLANG}" SAMAT_LLD_LINK="${SAMAT_LLD_LINK}")

add_executable(samat_cli src/Samat/CLI/Main.cpp)
set_target_properties(samat_cli PROPERTIES OUTPUT_NAME Samat)
target_link_libraries(samat_cli PRIVATE Samat::Core)
if(MSVC)
    target_compile_options(samat_cli PRIVATE /EHs /utf-8)
endif()

if(WIN32)
    add_executable(samat_studio WIN32 src/Samat/Studio/Main.cpp)
    set_target_properties(samat_studio PROPERTIES OUTPUT_NAME SamatStudio)
    target_link_libraries(samat_studio PRIVATE Samat::Core comdlg32 comctl32)
    target_compile_definitions(samat_studio PRIVATE
        SAMAT_STUDIO_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples/Samat/v1.0")
    if(MSVC)
        target_compile_options(samat_studio PRIVATE /EHs /utf-8)
    endif()
    install(TARGETS samat_studio RUNTIME DESTINATION bin COMPONENT Samat)
endif()

if(BUILD_TESTING)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/runner-lf.in" "7\n*\n7")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/runner-crlf.in" "7\r\n*\r\n7\r\n")
    foreach(case IN ITEMS lf crlf)
        add_test(NAME samat_runner_${case}
            COMMAND ${CMAKE_COMMAND}
                "-DPROGRAM=$<TARGET_FILE:samat_cli>"
                "-DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/main.st"
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/runner-${case}.in"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/RunnerSmoke.cmake")
    endforeach()

    foreach(test_name IN ITEMS compiler extended native_data safe_live)
        if(test_name STREQUAL "compiler")
            set(test_source tests/CompilerRegression.cpp)
            set(test_target samat_compiler_tests)
        elseif(test_name STREQUAL "extended")
            set(test_source tests/ExtendedRegression.cpp)
            set(test_target samat_extended_tests)
        elseif(test_name STREQUAL "native_data")
            set(test_source tests/NativeDataRegression.cpp)
            set(test_target samat_native_data_tests)
        else()
            set(test_source tests/SafeLiveRegression.cpp)
            set(test_target samat_safe_live_tests)
        endif()
        add_executable(${test_target} ${test_source})
        target_link_libraries(${test_target} PRIVATE Samat::Core)
        target_compile_definitions(${test_target} PRIVATE SAMAT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        if(MSVC)
            target_compile_options(${test_target} PRIVATE /EHs /utf-8)
        endif()
        add_test(NAME samat_${test_name}_regression COMMAND ${test_target})
    endforeach()

    add_executable(samat_haerye_tests tests/HaeryeRegression.cpp)
    target_link_libraries(samat_haerye_tests PRIVATE Samat::Core)
    target_compile_definitions(samat_haerye_tests PRIVATE SAMAT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME samat_haerye_regression COMMAND samat_haerye_tests)

    add_executable(samat_runtime_memory tests/RuntimeMemory.cpp)
    target_link_libraries(samat_runtime_memory PRIVATE Samat::Core)
    if(MSVC)
        target_compile_options(samat_runtime_memory PRIVATE /EHs /utf-8)
    endif()
    add_test(NAME samat_runtime_memory COMMAND samat_runtime_memory)
endif()

if(SAMAT_BUILD_BENCHMARKS)
    add_executable(samat_benchmark tests/CompilerBenchmark.cpp)
    target_link_libraries(samat_benchmark PRIVATE Samat::Core)
endif()

install(TARGETS samat_cli samat_runtime
    RUNTIME DESTINATION bin COMPONENT Samat
    ARCHIVE DESTINATION bin COMPONENT Samat)
install(FILES main.st DESTINATION examples COMPONENT Samat)
install(DIRECTORY examples/Samat/v1.0 DESTINATION examples/Samat COMPONENT Samat)
install(DIRECTORY examples/Haerye DESTINATION examples COMPONENT Samat)
install(DIRECTORY docs/ DESTINATION docs COMPONENT Samat)
install(FILES README.md DESTINATION . COMPONENT Samat)
set(CPACK_PACKAGE_NAME "Samat")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
if(WIN32)
    set(CPACK_PACKAGE_FILE_NAME "Samat-${PROJECT_VERSION}-Windows-x64")
else()
    set(CPACK_PACKAGE_FILE_NAME "Samat-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(CPACK_GENERATOR "ZIP")
include(CPack)
