# ─────────────────────────────────────────────────────────────────────────────
# Hub (TAC) unit tests — links glt_core_hub and the GLT_TARGET_HUB macro.
#
# Additive: the Beta suite in ../CMakeLists.txt is untouched. Reuses the same
# zero-dependency harness (../harness.hpp, ../test_main.cpp).
# ─────────────────────────────────────────────────────────────────────────────
add_executable(glt_tests_hub
    ../test_main.cpp
    test_paths_hub.cpp
    test_procs_hub.cpp
    test_perf_hub.cpp
    test_keymaps_hub.cpp
    test_fixes_hub.cpp
    test_variant_hub.cpp)

target_link_libraries(glt_tests_hub PRIVATE glt_core_hub)

# ../  so `#include "harness.hpp"` resolves, and this dir for hub_test_util.hpp.
target_include_directories(glt_tests_hub PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_compile_definitions(glt_tests_hub PRIVATE
    GLT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data")

if(MSVC)
    # Same policy as the Beta suite: readable failures, no _DEBUG (which would
    # flip to the debug CRT and mismatch glt_core_hub's /MT objects).
    target_compile_options(glt_tests_hub PRIVATE /W4 /EHsc /utf-8)
    target_link_options(glt_tests_hub PRIVATE /DEBUG)
endif()

add_test(NAME glt_unit_hub COMMAND glt_tests_hub)
set_tests_properties(glt_unit_hub PROPERTIES TIMEOUT 120)