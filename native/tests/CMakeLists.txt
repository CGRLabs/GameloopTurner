# ─────────────────────────────────────────────────────────────────────────────
# Unit tests — pure logic only, no GameLoop required.
#
# Two independent suites:
#   glt_unit      — Beta (links glt_core)          — UNCHANGED, the baseline.
#   glt_unit_hub  — Hub (links glt_core_hub)        — native/tests/hub/.
#
# These cover the parts where a silent divergence would disable the fix without
# producing any visible error: the smk.conf <Tex> matcher (which depends on a
# multi-line layout), the SwitchOperation stripper, blob flatness counting, the
# version sort, the secret scrubber, and the registry / path constants.
#
# Each executable returns the number of failures and is wired into CTest.
# ─────────────────────────────────────────────────────────────────────────────

if(GLT_TARGET_BETA)
    add_executable(glt_tests
        test_main.cpp
        test_strings.cpp
        test_keymaps.cpp
        test_perf.cpp
        test_fixes.cpp
        test_fonts.cpp
        test_layout.cpp
        test_system.cpp)

    target_link_libraries(glt_tests PRIVATE glt_core)
    target_include_directories(glt_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

    target_compile_definitions(glt_tests PRIVATE
        GLT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

    if(MSVC)
        # Debug-symbol-friendly: a test binary benefits far more from /Zi and
        # incremental linking than the shipping exe does.
        #
        # NOTE: _DEBUG must NOT be defined here. glt_core is built with the release
        # static CRT (/MT); defining _DEBUG on the test TU alone flips it to the
        # debug CRT (/MTd) and every object mismatches at link time with
        # LNK2038 RuntimeLibrary / _ITERATOR_DEBUG_LEVEL.
        target_compile_options(glt_tests PRIVATE /W4 /EHsc /utf-8)
        target_link_options(glt_tests PRIVATE /DEBUG)
    endif()

    add_test(NAME glt_unit COMMAND glt_tests)

    # A failing test must fail the build, not scroll past.
    set_tests_properties(glt_unit PROPERTIES TIMEOUT 120)
endif()

# Hub-specific suite. Additive: it never touches the Beta tests above.
if(GLT_TARGET_HUB)
    add_subdirectory(hub)
endif()