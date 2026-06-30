cmake_minimum_required(VERSION 3.26)
project(xp_wellys_vfr_trainer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTS "Build Catch2 unit tests" ON)

# ── ccache (optional) ────────────────────────────────────────────────────────
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "ccache enabled: ${CCACHE_PROGRAM}")
endif()

# ── Version from VERSION.txt ─────────────────────────────────────────────────
file(READ "${CMAKE_SOURCE_DIR}/VERSION.txt" VERSION_STRING)
string(STRIP "${VERSION_STRING}" VERSION_STRING)

# ── Architecture + deployment target ─────────────────────────────────────────
# The trainer is cloud-only (libcurl + system frameworks, no local ML), so it
# is architecture-neutral. A single configure with both architectures produces
# a universal .xpl directly — no lipo merge needed (unlike the ATC template,
# whose arm64-only Metal/onnxruntime backends forced a two-stage build).
if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0")
endif()

# ── Dear ImGui ────────────────────────────────────────────────────────────────
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/vendor/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp
)

# libcurl backs the OpenAI / Mistral cloud clients.
find_package(CURL REQUIRED)

# ── Engine OBJECT library (X-Plane SDK-free) ─────────────────────────────────
# Must compile and link without any XPLM / XPWidgets symbols. The async LM
# dispatcher (manager.cpp) and the provider-agnostic helpers (openai_common.cpp)
# touch only std::thread + libcurl + the abstract interface, so the Catch2 tests
# can reuse them without pulling in the X-Plane SDK.
add_library(xp_trainer_engine OBJECT
    src/core/logging.cpp
    src/backends/manager.cpp
    src/backends/openai_common.cpp
    src/airports/geo.cpp
    src/airports/apt_dat_parser.cpp
    src/airports/airport_db.cpp
    src/preflight/flight_suggester.cpp
)
target_include_directories(xp_trainer_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/vendor
    ${CMAKE_SOURCE_DIR}/src
)
target_compile_definitions(xp_trainer_engine PRIVATE
    XP_WELLYS_TRAINER_VERSION="${VERSION_STRING}"
)
if(APPLE)
    target_compile_options(xp_trainer_engine PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -fvisibility=hidden
    )
endif()

# ── Plugin MODULE (engine + X-Plane-specific) ────────────────────────────────
add_library(xp_wellys_vfr_trainer MODULE
    $<TARGET_OBJECTS:xp_trainer_engine>
    src/main.cpp
    src/backends/openai_lm.cpp
    src/backends/mistral_lm.cpp
    src/backends/loader.cpp
    src/persistence/settings.cpp
    src/persistence/keychain.cpp
    src/ui/trainer_ui.cpp
    ${IMGUI_SOURCES}
)
target_include_directories(xp_wellys_vfr_trainer PRIVATE
    ${CMAKE_SOURCE_DIR}/sdk
    ${CMAKE_SOURCE_DIR}/sdk/XPLM
    ${CMAKE_SOURCE_DIR}/sdk/XPWidgets
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/vendor
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

set_target_properties(xp_wellys_vfr_trainer PROPERTIES
    OUTPUT_NAME "xp_wellys_vfr_trainer"
    SUFFIX      ".xpl"
    PREFIX      ""
)

if(APPLE)
    target_compile_definitions(xp_wellys_vfr_trainer PRIVATE
        APL=1 IBM=0 LIN=0
        XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM400=1
        XP_WELLYS_TRAINER_VERSION="${VERSION_STRING}"
    )

    # Suppress OpenGL deprecation warnings in our own GL code only
    # (ImGui defines its own).
    set_source_files_properties(
        src/main.cpp src/ui/trainer_ui.cpp
        PROPERTIES COMPILE_DEFINITIONS "GL_SILENCE_DEPRECATION"
    )
    target_compile_options(xp_wellys_vfr_trainer PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -fvisibility=hidden
    )

    set(SDK_MAC_LIBS "${CMAKE_SOURCE_DIR}/sdk/Libraries/Mac")
    target_link_libraries(xp_wellys_vfr_trainer PRIVATE
        "${SDK_MAC_LIBS}/XPLM.framework/Versions/Current/XPLM"
        "${SDK_MAC_LIBS}/XPWidgets.framework/Versions/Current/XPWidgets"
        "-framework OpenGL"
        "-framework CoreFoundation"
        # Security.framework backs persistence/keychain.cpp (API keys in the
        # macOS Keychain).
        "-framework Security"
        # libcurl backs the OpenAI / Mistral cloud clients.
        CURL::libcurl
    )

    set_target_properties(xp_wellys_vfr_trainer PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        INSTALL_RPATH "@loader_path"
    )
endif()

# ── Tests ────────────────────────────────────────────────────────────────────
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
