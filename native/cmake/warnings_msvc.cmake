# ─────────────────────────────────────────────────────────────────────────────
# MSVC warning / optimisation policy.
#
# Our own code is held to /W4 with warnings-as-errors. Third-party translation
# units (the SQLite amalgamation, Dear ImGui) are compiled with the project's
# global flags but their own warning level is dropped to /W1 — chasing upstream
# diagnostics costs build time and adds no safety on our behalf.
# ─────────────────────────────────────────────────────────────────────────────

if(NOT MSVC)
    return()
endif()

# Global: everything except the per-target /O1 /GL block in CMakeLists.txt.
add_compile_options(
    /W4          # high warning level for our code
    /permissive- # standards-conforming
    /Zc:preprocessor
    /bigobj)

# Third-party escape hatch.
set(GLT_THIRDPARTY_WARN_FLAGS /W1 /wd4100 /wd4127 /wd4244 /wd4267 /wd4324
    CACHE INTERNAL "warnings disabled for third-party code")

# Diagnostics that fire on legitimate, intentional C++20 code in this codebase.
add_compile_options(
    /wd4996    # deprecated CRT calls — we still call _wfopen etc. on purpose
    /wd4459    # declaration hides global — ImGui macros do this everywhere
    /wd4324    # structure padded due to alignment specifier
    /wd4267    # size_t -> smaller type conversion
    /wd4244    # narrowing conversion
    /wd4100    # unreferenced formal parameter
    /wd4127    # conditional expression is constant
)