cmake_minimum_required(VERSION 3.21)

if(APPLE)
    project(MikuMikuWorld
        VERSION 1.0.0
        LANGUAGES C CXX OBJC OBJCXX
    )
elseif(UNIX)
    # Linux builds only ship the headless offline renderer (mmw-render).
    project(MikuMikuWorld
        VERSION 1.0.0
        LANGUAGES C CXX
    )
else()
    message(FATAL_ERROR "MikuMikuWorld supports macOS (full editor) and Linux (headless mmw-render). Windows support was dropped during the Cocoa port.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.14" CACHE STRING "Minimum macOS version")
endif()

# ---------------------------------------------------------------------------
# External dependencies fetched at configure time
# ---------------------------------------------------------------------------
include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
    PATCH_COMMAND  git apply --ignore-whitespace --reject "${CMAKE_CURRENT_SOURCE_DIR}/patches/glfw-cocoa-ime.patch" || true
    UPDATE_DISCONNECTED TRUE
)

set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG        v1.3.1
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(glfw zlib)

# zlib's CMake doesn't propagate include directories on its static target in
# older releases. Force it here so translation units that #include <zlib.h>
# resolve via the fetched source.
target_include_directories(zlibstatic PUBLIC
    $<BUILD_INTERFACE:${zlib_SOURCE_DIR}>
    $<BUILD_INTERFACE:${zlib_BINARY_DIR}>
)

# ---------------------------------------------------------------------------
# Source layout
# ---------------------------------------------------------------------------
set(MMW_SRC_DIR ${CMAKE_SOURCE_DIR}/MikuMikuWorld)
set(DEPS_DIR   ${CMAKE_SOURCE_DIR}/Depends)

# Core sources shared by the GUI (.app) and CLI (mmw-render) targets. Entry
# points (main.cpp, main_cli.cpp) and platform-specific files live below.
set(MMW_CORE_SOURCES
    ${MMW_SRC_DIR}/Application.cpp
    ${MMW_SRC_DIR}/ApplicationConfiguration.cpp
    ${MMW_SRC_DIR}/Background.cpp
    ${MMW_SRC_DIR}/BinaryReader.cpp
    ${MMW_SRC_DIR}/BinaryWriter.cpp
    ${MMW_SRC_DIR}/Clipboard.cpp
    ${MMW_SRC_DIR}/DebugEffectView.cpp
    ${MMW_SRC_DIR}/EffectView.cpp
    ${MMW_SRC_DIR}/File.cpp
    ${MMW_SRC_DIR}/HistoryManager.cpp
    ${MMW_SRC_DIR}/ImGuiManager.cpp
    ${MMW_SRC_DIR}/InputBinding.cpp
    ${MMW_SRC_DIR}/IO.cpp
    ${MMW_SRC_DIR}/Jacket.cpp
    ${MMW_SRC_DIR}/JsonIO.cpp
    ${MMW_SRC_DIR}/Language.cpp
    ${MMW_SRC_DIR}/Localization.cpp
    ${MMW_SRC_DIR}/MathUtils.cpp
    ${MMW_SRC_DIR}/MinMax.cpp
    ${MMW_SRC_DIR}/NativeScoreSerializer.cpp
    ${MMW_SRC_DIR}/Note.cpp
    ${MMW_SRC_DIR}/NotesFilter.cpp
    ${MMW_SRC_DIR}/AggregateNotesFilter.cpp
    ${MMW_SRC_DIR}/NoteSkin.cpp
    ${MMW_SRC_DIR}/NotesPreset.cpp
    ${MMW_SRC_DIR}/OfflineRenderer.cpp
    ${MMW_SRC_DIR}/OpenGlLoader.cpp
    ${MMW_SRC_DIR}/Particle.cpp
    ${MMW_SRC_DIR}/PreviewData.cpp
    ${MMW_SRC_DIR}/PreviewEngine.cpp
    ${MMW_SRC_DIR}/ResourceManager.cpp
    ${MMW_SRC_DIR}/Score.cpp
    ${MMW_SRC_DIR}/ScoreContext.cpp
    ${MMW_SRC_DIR}/ScoreEditor.cpp
    ${MMW_SRC_DIR}/ScoreEditorTimeline.cpp
    ${MMW_SRC_DIR}/ScoreEditorWindows.cpp
    ${MMW_SRC_DIR}/ScorePreview.cpp
    ${MMW_SRC_DIR}/Preview/Overlay.cpp
    ${MMW_SRC_DIR}/Preview/OverlayAssets.cpp
    ${MMW_SRC_DIR}/Preview/OverlayText.cpp
    ${MMW_SRC_DIR}/Preview/VideoPlayer.cpp
    ${MMW_SRC_DIR}/ScoreSerializeWindow.cpp
    ${MMW_SRC_DIR}/ScoreSerializer.cpp
    ${MMW_SRC_DIR}/ScoreStats.cpp
    ${MMW_SRC_DIR}/Sonolus.cpp
    ${MMW_SRC_DIR}/SonolusSerializer.cpp
    ${MMW_SRC_DIR}/Stopwatch.cpp
    ${MMW_SRC_DIR}/SusExporter.cpp
    ${MMW_SRC_DIR}/SusParser.cpp
    ${MMW_SRC_DIR}/SusSerializer.cpp
    ${MMW_SRC_DIR}/UscSerializer.cpp
    ${MMW_SRC_DIR}/Tempo.cpp
    ${MMW_SRC_DIR}/UI.cpp
    ${MMW_SRC_DIR}/Utilities.cpp

    ${MMW_SRC_DIR}/Audio/AudioManager.cpp
    ${MMW_SRC_DIR}/Audio/Sound.cpp

    ${MMW_SRC_DIR}/Rendering/Camera.cpp
    ${MMW_SRC_DIR}/Rendering/Framebuffer.cpp
    ${MMW_SRC_DIR}/Rendering/Renderer.cpp
    ${MMW_SRC_DIR}/Rendering/Shader.cpp
    ${MMW_SRC_DIR}/Rendering/Texture.cpp

    ${MMW_SRC_DIR}/ImGui/imgui.cpp
    ${MMW_SRC_DIR}/ImGui/imgui_demo.cpp
    ${MMW_SRC_DIR}/ImGui/imgui_draw.cpp
    ${MMW_SRC_DIR}/ImGui/imgui_impl_glfw.cpp
    ${MMW_SRC_DIR}/ImGui/imgui_impl_opengl3.cpp
    ${MMW_SRC_DIR}/ImGui/imgui_stdlib.cpp
    ${MMW_SRC_DIR}/ImGui/imgui_tables.cpp
    ${MMW_SRC_DIR}/ImGui/imgui_widgets.cpp
)

if(APPLE)
    list(APPEND MMW_PLATFORM_SOURCES
        ${MMW_SRC_DIR}/Platform/Cocoa/MessageBox.mm
        ${MMW_SRC_DIR}/Platform/Cocoa/FileDialog.mm
        ${MMW_SRC_DIR}/Platform/Cocoa/Locale.mm
        ${MMW_SRC_DIR}/Platform/Cocoa/Paths.cpp
    )
else()
    list(APPEND MMW_PLATFORM_SOURCES
        ${MMW_SRC_DIR}/Platform/Linux/MessageBox.cpp
        ${MMW_SRC_DIR}/Platform/Linux/FileDialog.cpp
        ${MMW_SRC_DIR}/Platform/Linux/Locale.cpp
        ${MMW_SRC_DIR}/Platform/Linux/Paths.cpp
    )
endif()

set(DEPS_SOURCES
    ${DEPS_DIR}/glad/src/glad.c
    ${DEPS_DIR}/stb_vorbis/stb_vorbis.c
)

# ---------------------------------------------------------------------------
# Shared core static library
# ---------------------------------------------------------------------------
add_library(mmw_core STATIC
    ${MMW_CORE_SOURCES}
    ${MMW_PLATFORM_SOURCES}
    ${DEPS_SOURCES}
)

target_include_directories(mmw_core PUBLIC
    ${MMW_SRC_DIR}
    ${MMW_SRC_DIR}/Platform/Stubs
    ${DEPS_DIR}/DirectXMath-master/Inc
    ${DEPS_DIR}/glad/include
    ${DEPS_DIR}/stb_image
    ${DEPS_DIR}/stb_truetype
    ${DEPS_DIR}/stb_vorbis
    ${DEPS_DIR}/miniaudio
    ${DEPS_DIR}/json
)

target_compile_definitions(mmw_core PUBLIC
    APP_VERSION_STRING="${PROJECT_VERSION}"
    IMGUI_DEFINE_MATH_OPERATORS
    GL_SILENCE_DEPRECATION
)

# DirectXMath pulls in <arm_neon.h> on Linux aarch64, which conflicts with
# libstdc++'s locale/codecvt internals. The editor only uses DirectXMath for
# small matrix ops so the scalar fallback is fast enough.
if(NOT APPLE)
    target_compile_definitions(mmw_core PUBLIC _XM_NO_INTRINSICS_)
endif()

target_link_libraries(mmw_core PUBLIC
    glfw
    zlibstatic
)

if(APPLE)
    find_library(COCOA_LIB Cocoa REQUIRED)
    find_library(OPENGL_LIB OpenGL REQUIRED)
    find_library(IOKIT_LIB IOKit REQUIRED)
    find_library(COREFOUNDATION_LIB CoreFoundation REQUIRED)
    find_library(APPKIT_LIB AppKit REQUIRED)

    target_link_libraries(mmw_core PUBLIC
        ${COCOA_LIB}
        ${OPENGL_LIB}
        ${IOKIT_LIB}
        ${COREFOUNDATION_LIB}
        ${APPKIT_LIB}
    )
else()
    find_package(OpenGL REQUIRED)
    find_package(X11 REQUIRED)
    target_link_libraries(mmw_core PUBLIC
        OpenGL::GL
        X11::X11
        ${CMAKE_DL_LIBS}
    )
endif()

# stb_vorbis and glad are third-party C with many warnings; silence them so
# real diagnostics stand out.
set_source_files_properties(
    ${DEPS_DIR}/stb_vorbis/stb_vorbis.c
    ${DEPS_DIR}/glad/src/glad.c
    PROPERTIES COMPILE_FLAGS "-w"
)

# ---------------------------------------------------------------------------
# CLI target (both macOS and Linux): headless offline renderer
# ---------------------------------------------------------------------------
add_executable(mmw-render
    ${MMW_SRC_DIR}/main_cli.cpp
)
target_link_libraries(mmw-render PRIVATE mmw_core)

# ---------------------------------------------------------------------------
# GUI target (macOS only): full editor shipped as a .app bundle
# ---------------------------------------------------------------------------
if(APPLE)
    set(MMW_ICON_FILE ${MMW_SRC_DIR}/res/mmw_icon.icns)
    set_source_files_properties(${MMW_ICON_FILE} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )

    add_executable(MikuMikuWorld MACOSX_BUNDLE
        ${MMW_SRC_DIR}/main.cpp
        ${MMW_ICON_FILE}
    )
    target_link_libraries(MikuMikuWorld PRIVATE mmw_core)

    set_target_properties(MikuMikuWorld PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME            "MikuMikuWorld"
        MACOSX_BUNDLE_GUI_IDENTIFIER         "com.crash5band.MikuMikuWorld"
        MACOSX_BUNDLE_BUNDLE_VERSION         "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING   "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT              "Copyright (c) crash5band"
        MACOSX_BUNDLE_ICON_FILE              "mmw_icon.icns"
        MACOSX_BUNDLE_INFO_PLIST             "${MMW_SRC_DIR}/Platform/Cocoa/Info.plist.in"
    )

    # Copy res/ into the bundle's Resources directory on every build. The bundle
    # output path varies by generator (Xcode puts binaries under $<CONFIG>/), so
    # use the generator expression for the bundle content dir.
    add_custom_command(TARGET MikuMikuWorld POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${MMW_SRC_DIR}/res
            $<TARGET_BUNDLE_CONTENT_DIR:MikuMikuWorld>/Resources/res
        COMMENT "Copying res/ into .app bundle"
        VERBATIM
    )
endif()

# For CLI builds on any platform, drop a `res/` directory next to the binary so
# Platform::getResourceDir() resolves to it without the bundle layout. Skipped
# on macOS if only the .app is being built; harmless otherwise.
add_custom_command(TARGET mmw-render POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${MMW_SRC_DIR}/res
        $<TARGET_FILE_DIR:mmw-render>/res
    COMMENT "Copying res/ next to mmw-render"
    VERBATIM
)
