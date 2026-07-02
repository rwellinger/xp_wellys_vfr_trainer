cmake_minimum_required(VERSION 3.26)
project(xp_wellys_vfr_trainer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTS "Build Catch2 unit tests" ON)

# ── Windows-wide compile definitions ─────────────────────────────────────────
# libcurl (and wincred.h in keychain.cpp) drag in <windows.h>. NOMINMAX keeps
# the min/max macros from clobbering std::min/max; WIN32_LEAN_AND_MEAN avoids
# the WinSock1/2 clash; _USE_MATH_DEFINES exposes M_PI etc. under MSVC.
if(WIN32)
    add_compile_definitions(
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
endif()

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
    src/airports/airport_score_cache.cpp
    src/airports/airport_scorer.cpp
    src/preflight/flight_suggester.cpp
    src/dependencies/dependencies.cpp
    src/postflight/flight_log.cpp
    src/postflight/atc_log.cpp
    src/postflight/correlation.cpp
    src/postflight/report_cache.cpp
    src/postflight/evaluator.cpp
    src/postflight/discovery.cpp
)
target_include_directories(xp_trainer_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/vendor
    ${CMAKE_SOURCE_DIR}/src
    # openai_common.cpp includes <curl/curl.h>. On Windows vcpkg exposes the
    # curl headers only through the imported target, not a system path, so the
    # engine OBJECT lib must pull them from CURL::libcurl explicitly.
    $<TARGET_PROPERTY:CURL::libcurl,INTERFACE_INCLUDE_DIRECTORIES>
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
    src/dependencies/xplm_plugin_probe.cpp
    src/fms/fms_writer.cpp
    src/ui/trainer_ui.cpp
    ${IMGUI_SOURCES}
)

# Platform clipboard bridge: NSPasteboard (.mm) on macOS, Win32 clipboard on
# Windows. The .mm must never reach MSVC (no ObjC++ frontend).
if(APPLE)
    target_sources(xp_wellys_vfr_trainer PRIVATE src/ui/clipboard.mm)
elseif(WIN32)
    target_sources(xp_wellys_vfr_trainer PRIVATE src/ui/clipboard_win.cpp)
endif()
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
        # XPLM410 enables the multi-FPL FMS API (xplm_Fpl_Pilot_Primary,
        # XPLMSetFMSFlightPlanEntryInfo) used by src/fms — requires X-Plane 12.1+.
        XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM400=1 XPLM410=1
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
        # AppKit (NSPasteboard) backs src/ui/clipboard.mm — the Settings tab's
        # [Paste] button reads the system clipboard for the API key.
        "-framework AppKit"
        # libcurl backs the OpenAI / Mistral cloud clients.
        CURL::libcurl
    )

    set_target_properties(xp_wellys_vfr_trainer PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        INSTALL_RPATH "@loader_path"
    )

elseif(WIN32)
    target_compile_definitions(xp_wellys_vfr_trainer PRIVATE
        APL=0 IBM=1 LIN=0
        XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM400=1 XPLM410=1
        XP_WELLYS_TRAINER_VERSION="${VERSION_STRING}"
    )
    # /utf-8: the sources are UTF-8; MSVC otherwise misreads non-ASCII literals.
    target_compile_options(xp_wellys_vfr_trainer PRIVATE /W3 /utf-8)

    set(SDK_WIN_LIBS "${CMAKE_SOURCE_DIR}/sdk/Libraries/Win")
    target_link_libraries(xp_wellys_vfr_trainer PRIVATE
        "${SDK_WIN_LIBS}/XPLM_64.lib"
        "${SDK_WIN_LIBS}/XPWidgets_64.lib"
        opengl32
        Ws2_32   # sockets for libcurl
        Crypt32  # Schannel TLS cert store (static curl)
        Bcrypt   # CNG RNG for curl
        Advapi32 # Credential Manager (persistence/keychain.cpp)
        CURL::libcurl
    )
endif()

# ── Tests ────────────────────────────────────────────────────────────────────
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
