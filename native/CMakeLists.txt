# ─────────────────────────────────────────────────────────────────────────────
# GameLoop Tuner — native C++20 build
#
# Target: two standalone variant .exe files with zero external DLL dependencies
# (Beta + Hub/TAC). Static CRT (/MT) + LTCG + size-optimised codegen keeps each
# in the ~1.5 MB budget. Admin rights are requested by the embedded manifest,
# not at runtime.
# ─────────────────────────────────────────────────────────────────────────────
cmake_minimum_required(VERSION 3.24)

project(GameLoopTuner
        VERSION 2.0.0
        DESCRIPTION "GameLoop Tuner — native"
        LANGUAGES C CXX)

# ── Global build settings ───────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# CMake's default Release flags for MSVC are "/O2 /Ob2 /DNDEBUG". We want size,
# not speed, so /O2 and /Ob2 are stripped and replaced with /O1 + /Os below.
# Leaving both in place produces D9025 on every single compile line.
string(REGEX REPLACE "/O2 /Ob2 /DNDEBUG" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
string(REGEX REPLACE "/O2 /Ob2 /DNDEBUG" "" CMAKE_C_FLAGS_RELEASE   "${CMAKE_C_FLAGS_RELEASE}")
string(REGEX REPLACE "/O2 /Ob2 /DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
string(REGEX REPLACE "/O2 /Ob2 /DNDEBUG" "" CMAKE_C_FLAGS_RELWITHDEBINFO   "${CMAKE_C_FLAGS_RELWITHDEBINFO}")

# Static runtime so vcruntime140.dll is never required beside the .exe.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ── Warnings / optimisation (MSVC) ──────────────────────────────────────────
include(cmake/warnings_msvc.cmake)
include(cmake/sqlite_flags.cmake)

# ── Resource compiler: UAC manifest + icon ──────────────────────────────────
enable_language(RC)
set(GLT_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/app.manifest")
set(GLT_ICON     "${CMAKE_CURRENT_BINARY_DIR}/mythic.ico")
configure_file(cmake/app.manifest.in "${GLT_MANIFEST}" COPYONLY)
configure_file("${CMAKE_SOURCE_DIR}/../mythic.ico" "${GLT_ICON}" COPYONLY)
set(GLT_RC "${CMAKE_CURRENT_BINARY_DIR}/glt_resources.rc")
configure_file(cmake/glt_resources.rc "${GLT_RC}" COPYONLY)

# ── Third party ──────────────────────────────────────────────────────────────
include(FetchContent)

# SQLite: official amalgamation. Pinned so the artifact is reproducible.
# FETCHCONTENT_QUIET off → we want to see progress in CI logs.
set(SQLITE_VERSION "3500400")
FetchContent_Declare(sqlite3
    URL      "https://sqlite.org/2025/sqlite-amalgamation-${SQLITE_VERSION}.zip"
    URL_HASH SHA256=1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# Dear ImGui: master branch pinned to a tag.
# NOTE: the *docking* branch is deliberately NOT used — this app has a fixed
# non-dockable sidebar layout, and docking drags in imgui_internal extras we
# would immediately strip for the size budget.
set(IMGUI_VERSION "v1.91.9b")
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        ${IMGUI_VERSION}
    GIT_SHALLOW    TRUE)

# nlohmann/json — single header.
set(JSON_VERSION "v3.11.3")
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        ${JSON_VERSION}
    GIT_SHALLOW    TRUE)

# A vendored copy under native/third_party/<dep>/ always wins, so a build with
# no network still works. This is CMake's documented override hook.
foreach(dep sqlite3 imgui json)
    string(TOUPPER "${dep}" DEP_UPPER)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/${dep}")
        set(FETCHCONTENT_SOURCE_DIR_${DEP_UPPER}
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/${dep}"
            CACHE PATH "Vendored ${dep} (overrides download)")
        message(STATUS "  ${dep}    : vendored (third_party/)")
    else()
        message(STATUS "  ${dep}    : will download")
    endif()
endforeach()

FetchContent_MakeAvailable(sqlite3 imgui json)

# ── Size-first optimisation ──────────────────────────────────────────────────
#
# Applied to EVERY target, third-party included. Leaving the amalgamation or
# ImGui at CMake's default /O2 /Ob2 and without /GL costs ~1 MB of the final
# image: /GL is what lets the linker dead-strip across translation-unit
# boundaries, and without it every function the amalgamation happens to define
# survives into the executable whether it is called or not.
#
# /O1 == /Os. /O2 is NOT combined with it: they are mutually exclusive and MSVC
# warns D9025 on every compile line.
function(glt_size_optimise target)
    if(NOT MSVC)
        return()
    endif()
    target_compile_options(${target} PRIVATE
        /O1        # favour small code
        /Os        # explicit alias of /O1, documents intent
        /GL        # whole-program optimisation -> lets the linker strip
        /Gy        # function-level linking
        /Gw        # optimise global data for locality
        /DNDEBUG
        /GS-       # no stack cookies; nothing here needs them
        /GR-       # no RTTI: ImGui/stb-style code never uses dynamic_cast
        /Zc:__cplusplus
        /Zc:inline
        /utf-8
        /EHsc)     # ImGui and SQLite are both C-ambiguous
endfunction()

# ── SQLite: compile the amalgamation straight into our target ────────────────
add_library(glt_sqlite3 STATIC "${sqlite3_SOURCE_DIR}/sqlite3.c")
target_include_directories(glt_sqlite3 SYSTEM PUBLIC "${sqlite3_SOURCE_DIR}")
target_compile_definitions(glt_sqlite3 PRIVATE ${GLT_SQLITE_DEFS})
glt_size_optimise(glt_sqlite3)
set_target_properties(glt_sqlite3 PROPERTIES
    C_CLANG_WARN_DOCUMENTATION_COMMENT OFF
    COMPILE_WARNING_AS_ERROR OFF)

# Omit the amalgamation's local CMake target if it defined one, so we own it.
if(TARGET sqlite3)
    set_target_properties(glt_sqlite3 PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

# ── ImGui: only the six translation units we actually need ───────────────────
add_library(glt_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp")
target_include_directories(glt_imgui SYSTEM PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends")
target_compile_definitions(glt_imgui PUBLIC
    UNICODE _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN)
target_link_libraries(glt_imgui PUBLIC d3d11 dxgi)
glt_size_optimise(glt_imgui)
# ImGui trips a lot of /W4 diagnostics inside its own macros; they are not ours
# to fix and chasing them costs build time without adding safety.
target_compile_options(glt_imgui PRIVATE /W1)

# ── nlohmann/json: header-only interface target ──────────────────────────────
add_library(glt_json INTERFACE)
target_include_directories(glt_json SYSTEM INTERFACE "${json_SOURCE_DIR}/include")

# ── Variant selection ────────────────────────────────────────────────────────
#
# Two builds, two binaries (docs/targets/README.md, beta/PLAN.md, hub/PLAN.md):
#
#   GLT_TARGET_BETA  -> GameLoopTuner-Beta.exe   (the original validated target)
#   GLT_TARGET_HUB   -> GameLoopTuner-Hub.exe    (the multi-app Arena/TAC build)
#
# Both default ON so a plain `cmake -S native -B build` produces both binaries.
# The macro is PUBLIC on the core library, so the executable AND the tests that
# link it inherit exactly one variant define. The Beta library keeps the name
# `glt_core` so the existing Beta test target is untouched.
option(GLT_TARGET_BETA "Build the GameLoop Beta variant" ON)
option(GLT_TARGET_HUB  "Build the GameLoop Hub (TAC) variant" ON)

if(NOT GLT_TARGET_BETA AND NOT GLT_TARGET_HUB)
    message(FATAL_ERROR
        "At least one variant is required: enable GLT_TARGET_BETA and/or GLT_TARGET_HUB.")
endif()

# UI/scaffolding, compiled once per variant. Provably identical across variants:
# it only reads App state and calls into the variant-specific core modules.
set(GLT_UI_SOURCES
    src/ui/theme.cpp
    src/ui/fonts.cpp
    src/ui/widgets.cpp
    src/ui/app.cpp
    src/ui/sidebar.cpp
    src/ui/variant_picker.cpp
    src/ui/page_dashboard.cpp
    src/ui/page_keymaps.cpp
    src/ui/page_performance.cpp
    src/ui/page_vm_priority.cpp
    src/ui/page_network.cpp
    src/ui/page_backups.cpp
    src/ui/page_logs.cpp
    src/ui/page_about.cpp)

# Core files whose behaviour the report marks SAME on Hub, so they are reused
# verbatim rather than duplicated (docs/targets/hub/PLAN.md: db/keymaps logic,
# registry, hosts). variant.cpp is shared but reads the build macro, so it is
# compiled per-variant like everything else here.
set(GLT_CORE_SHARED
    src/core/strings.cpp
    src/core/log.cpp
    src/core/registry.cpp
    src/core/hosts.cpp
    src/core/backups.cpp
    src/core/journal.cpp
    src/core/disk.cpp
    src/core/drift.cpp
    src/core/netinfo.cpp
    src/core/report.cpp
    src/core/db.cpp
    src/core/keymaps.cpp
    src/core/variant.cpp)

# Anything touching SQL / paths / procs / perf gets a per-variant file, never an
# #ifdef (README: "Prefer SEPARATE files ... Duplicate code is acceptable").
set(GLT_CORE_BETA_VARIANT
    src/core/paths.cpp
    src/core/procs.cpp
    src/core/perf.cpp
    src/core/fixes.cpp)

set(GLT_CORE_HUB_VARIANT
    src/core/paths_hub.cpp
    src/core/procs_hub.cpp
    src/core/perf_hub.cpp
    src/core/keymaps_hub.cpp
    src/core/fixes_hub.cpp)

# Applies the shared include dirs, defines and system libs to a variant core.
function(glt_configure_variant target macro)
    target_include_directories(${target} PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_include_directories(${target} SYSTEM PUBLIC
        "${imgui_SOURCE_DIR}"
        "${imgui_SOURCE_DIR}/backends"
        "${sqlite3_SOURCE_DIR}"
        "${json_SOURCE_DIR}/include")

    target_compile_definitions(${target} PUBLIC
        UNICODE _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
        # Windows 10. winsvc.h gates SERVICE_AUTO_START_DELAYED on NTDDI_WIN10 and
        # several D3D11 / DPI entry points on the same level. Without these the SDK
        # defaults to an older target and those symbols are simply absent.
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
        NTDDI_VERSION=0x0A000000
        ${macro})

    target_link_libraries(${target} PUBLIC
        glt_sqlite3
        glt_imgui
        glt_json
        d3d11 dxgi advapi32 shell32 user32 psapi ole32 gdi32 shlwapi dwmapi
        # comctl32: InitCommonControlsEx (common controls / themed dialogs).
        # ws2_32:   getaddrinfo + socket for the hosts-file probe.
        comctl32 ws2_32)

    glt_size_optimise(${target})
endfunction()

# ── Core library (everything except WinMain) so the tests can link it ───────
if(GLT_TARGET_BETA)
    add_library(glt_core STATIC
        ${GLT_CORE_SHARED}
        ${GLT_UI_SOURCES}
        ${GLT_CORE_BETA_VARIANT})
    glt_configure_variant(glt_core GLT_TARGET_BETA)
endif()

if(GLT_TARGET_HUB)
    add_library(glt_core_hub STATIC
        ${GLT_CORE_SHARED}
        ${GLT_UI_SOURCES}
        ${GLT_CORE_HUB_VARIANT})
    glt_configure_variant(glt_core_hub GLT_TARGET_HUB)
endif()

# ── The executables ──────────────────────────────────────────────────────────
#
# One executable per enabled variant. Both embed the same icon/manifest .rc and
# differ only in which core library they link and the output name.
function(glt_add_app target core out_name)
    add_executable(${target} WIN32
        src/main.cpp
        "${GLT_RC}")

    target_link_libraries(${target} PRIVATE ${core})

    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${out_name}"
        # /MANIFEST:NO stops the linker synthesising its own manifest. Ours is
        # embedded by cmake/glt_resources.rc as resource ID 1; letting both exist
        # makes mt.exe fail with LNK1327 / c1010001 because the linker's default
        # says asInvoker while ours says requireAdministrator.
        LINK_FLAGS "/ENTRY:wWinMainCRTStartup /MANIFEST:NO")

    if(MSVC)
        # A build cannot replace an executable that is currently running, and
        # because this binary is elevated an unelevated build shell has no
        # permission to see why the file is locked. Without this check the failure
        # surfaces as a bare "LNK1104: cannot open file ...exe".
        add_custom_command(TARGET ${target} PRE_LINK
            COMMAND ${CMAKE_COMMAND}
                -DPROJECT_TUNER_EXE=$<TARGET_FILE:${target}>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_not_running.cmake
            VERBATIM
            COMMENT "Checking that ${out_name}.exe is not already running")

        # The .rc embeds IDI_ICON1 (mythic.ico) and RT_MANIFEST 1.
        target_compile_options(${target} PRIVATE "/I${CMAKE_CURRENT_BINARY_DIR}")

        glt_size_optimise(${target})

        target_link_options(${target} PRIVATE
            /LTCG     # link-time code generation
            /OPT:REF  # drop unreferenced COMDATs
            /OPT:ICF) # fold identical COMDATs

        target_link_options(${core} INTERFACE
            /LTCG /OPT:REF /OPT:ICF)
    endif()
endfunction()

if(GLT_TARGET_BETA)
    glt_add_app(GameLoopTunerBeta  glt_core     "GameLoopTuner-Beta")
endif()

if(GLT_TARGET_HUB)
    glt_add_app(GameLoopTunerHub   glt_core_hub "GameLoopTuner-Hub")
endif()

# ── Installer ──────────────────────────────────────────────────────────────────
# GameLoopTunerSetup.exe — a deliberately separate, minimal target.
#
# It must NOT link glt_core. Doing so would drag imgui and the DX11 backend in
# and turn a 180 KB installer into ~1.7 MB, which is the entire reason this
# replaces the Inno Setup script: the product is one self-contained file, and
# the setup around it should be an order of magnitude smaller than it.
option(GLT_BUILD_INSTALLER "Build the GameLoop Tuner setup executable" ON)
if(GLT_BUILD_INSTALLER)
    set(GLT_SETUP_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/setup.manifest")
    set(GLT_SETUP_RC       "${CMAKE_CURRENT_BINARY_DIR}/setup_resources.rc")
    configure_file(installer/setup.manifest.in "${GLT_SETUP_MANIFEST}" COPYONLY)
    configure_file(installer/setup_resources.rc  "${GLT_SETUP_RC}" COPYONLY)

    add_executable(GLTSetup WIN32
        installer/main.cpp
        installer/setup.cpp
        installer/ui.cpp
        "${GLT_SETUP_RC}")

    set_target_properties(GLTSetup PROPERTIES
        OUTPUT_NAME "GameLoopTunerSetup"
        # Same manifest arrangement as the app: ours is embedded by the .rc as
        # RT_MANIFEST 1, so the linker must not synthesise a competing one.
        # Note this one is asInvoker, not requireAdministrator.
        LINK_FLAGS "/ENTRY:wWinMainCRTStartup /MANIFEST:NO")

    # The .rc names "mythic.ico" and "setup.manifest" by relative path, so the
    # binary dir (where both are configure_file'd) has to be on rc.exe's
    # include path.
    target_compile_options(GLTSetup PRIVATE "/I${CMAKE_CURRENT_BINARY_DIR}")

    # Mirrors the glt_core PUBLIC set, minus the CRT warning suppression that
    # only the app's own sources needed.
    target_compile_definitions(GLTSetup PRIVATE
        UNICODE _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
        NTDDI_VERSION=0x0A000000
        # Single source of truth for the version: project(... VERSION ...).
        GLT_VERSION="${PROJECT_VERSION}")

    # System libraries only. Anything beyond this list should be a deliberate
    # decision, because each one is a reason the file grows.
    #   shell32  SHGetKnownFolderPath, ShellExecuteW (runas re-launch)
    #   ole32    CoCreateInstance for the IShellLink shortcuts
    #   advapi32 HKLM\...\Uninstall registry writes
    #   gdi32    the hand-painted window
    target_link_libraries(GLTSetup PRIVATE
        user32 gdi32 shell32 ole32 advapi32)

    if(MSVC)
        glt_size_optimise(GLTSetup)
        target_link_options(GLTSetup PRIVATE /LTCG /OPT:REF /OPT:ICF)
    endif()
endif()

# ── Tests ────────────────────────────────────────────────────────────────────
option(GLT_BUILD_TESTS "Build the pure-logic unit tests" ON)
if(GLT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Install ──────────────────────────────────────────────────────────────────
if(GLT_TARGET_BETA)
    install(TARGETS GameLoopTunerBeta RUNTIME DESTINATION .)
endif()
if(GLT_TARGET_HUB)
    install(TARGETS GameLoopTunerHub RUNTIME DESTINATION .)
endif()
if(GLT_BUILD_INSTALLER)
    install(TARGETS GLTSetup RUNTIME DESTINATION .)
endif()

message(STATUS "GameLoop Tuner ${PROJECT_VERSION}")
message(STATUS "  build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  variants   : Beta=${GLT_TARGET_BETA} Hub=${GLT_TARGET_HUB}")
message(STATUS "  imgui      : ${IMGUI_VERSION}")
message(STATUS "  sqlite3    : ${SQLITE_VERSION}")
message(STATUS "  json       : ${JSON_VERSION}")
message(STATUS "  manifest   : ${GLT_MANIFEST}")