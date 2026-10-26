# The test compiles the library's source itself, to reach its internal functions.
add_executable(test_sprat_sdl test_sprat_sdl.c)
target_link_libraries(test_sprat_sdl PRIVATE SDL3::SDL3 sprat::sprat)
set_target_properties(test_sprat_sdl PROPERTIES ${SPRAT_SDL_C_STANDARD})
target_include_directories(test_sprat_sdl PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(test_sprat_sdl PRIVATE
    SPRAT_SDL_EXAMPLE_SHEET="${PROJECT_SOURCE_DIR}/examples/resources/sheet.sprat")
if(MSVC)
    target_compile_options(test_sprat_sdl PRIVATE /W4)
else()
    target_compile_options(test_sprat_sdl PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()
# The software renderer draws turned frames by their corners, so it runs the tests on its own too
add_test(NAME test_sprat_sdl COMMAND test_sprat_sdl)
add_test(NAME test_sprat_sdl_software COMMAND test_sprat_sdl)
# With SPRAT_SDL_SANITIZE: the GPU driver and D-Bus leak when SDL opens the offscreen window.
# Full stack unwinding lets the suppressions see through the driver, which is unloaded at exit.
set(SPRAT_SDL_TEST_ENVIRONMENT
    "ASAN_OPTIONS=fast_unwind_on_malloc=0"
    "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp:print_suppressions=0")
set_tests_properties(test_sprat_sdl PROPERTIES ENVIRONMENT "${SPRAT_SDL_TEST_ENVIRONMENT}")
set_tests_properties(test_sprat_sdl_software PROPERTIES ENVIRONMENT
    "${SPRAT_SDL_TEST_ENVIRONMENT};SDL_RENDER_DRIVER=software;SPRAT_TEST_REQUIRE_HARDWARE=0")
