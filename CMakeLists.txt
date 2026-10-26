cmake_minimum_required(VERSION 3.24)

# VERSION holds "vMAJOR.MINOR.PATCH", like sprat-lib.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SPRAT_SDL_VERSION_TAG)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS VERSION)
string(STRIP "${SPRAT_SDL_VERSION_TAG}" SPRAT_SDL_VERSION_TAG)
string(REGEX REPLACE "^v" "" SPRAT_SDL_VERSION "${SPRAT_SDL_VERSION_TAG}")

project(sprat-sdl VERSION ${SPRAT_SDL_VERSION} LANGUAGES C)
include(GNUInstallDirs)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C99 without extensions, set on sprat-sdl's own targets: as CMAKE_C_STANDARD and
# CMAKE_C_EXTENSIONS it would reach a downloaded SDL3 too, which needs GNU C on Emscripten.
set(SPRAT_SDL_C_STANDARD C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

option(BUILD_SHARED_LIBS "Build sprat-sdl as a shared library" OFF)
option(SPRAT_SDL_BUILD_EXAMPLES "Build the example programs" ${PROJECT_IS_TOP_LEVEL})
option(SPRAT_SDL_SANITIZE "Build with AddressSanitizer and UBSan (GCC/Clang)" OFF)

if(SPRAT_SDL_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Keep the DLLs next to the tests and examples so Windows can load them.
if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
endif()

# Dependencies: an installed package when there is one, otherwise a download.
# To build against a local checkout, pass -DFETCHCONTENT_SOURCE_DIR_SPRAT=../sprat-lib
# (or FETCHCONTENT_SOURCE_DIR_SDL3).
include(FetchContent)
if(NOT TARGET SDL3::SDL3)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.18
        GIT_SHALLOW TRUE
        FIND_PACKAGE_ARGS 3.4 CONFIG
    )
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
endif()
if(NOT TARGET sprat::sprat)
    FetchContent_Declare(sprat
        GIT_REPOSITORY https://github.com/pedroac/sprat-lib.git
        GIT_TAG main   # sprat_escape() is new in v0.6.0: pin its commit once it is published
        GIT_SHALLOW TRUE
        FIND_PACKAGE_ARGS 0.6
    )
    FetchContent_MakeAvailable(sprat)
    get_target_property(SPRAT_SDL_SPRAT_VERSION sprat::sprat VERSION)
    if(SPRAT_SDL_SPRAT_VERSION AND SPRAT_SDL_SPRAT_VERSION VERSION_LESS 0.6)
        message(FATAL_ERROR "sprat-sdl needs sprat-lib 0.6 or newer, found ${SPRAT_SDL_SPRAT_VERSION}")
    endif()
endif()

add_library(sprat-sdl src/SDL_sprat.c)
add_library(sprat-sdl::sprat-sdl ALIAS sprat-sdl)
target_include_directories(sprat-sdl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(sprat-sdl PUBLIC SDL3::SDL3 PRIVATE sprat::sprat)
set_target_properties(sprat-sdl PROPERTIES
    ${SPRAT_SDL_C_STANDARD}
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
# SDL_DECLSPEC exports the API from a DLL built with DLL_EXPORT, as SDL's own libraries do.
if(BUILD_SHARED_LIBS AND WIN32)
    target_compile_definitions(sprat-sdl PRIVATE DLL_EXPORT)
endif()

if(MSVC)
    target_compile_options(sprat-sdl PRIVATE /W4)
else()
    target_compile_options(sprat-sdl PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

if(SPRAT_SDL_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

include(CTest)
if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
    add_subdirectory(tests)
endif()

# Installing exports the target for find_package(), which only works when SDL3 and sprat
# are installed packages too: a downloaded SDL3 is in no export set.
get_target_property(SPRAT_SDL_SDL3_IMPORTED SDL3::SDL3 IMPORTED)
get_target_property(SPRAT_SDL_SPRAT_IMPORTED sprat::sprat IMPORTED)
if(SPRAT_SDL_SDL3_IMPORTED AND SPRAT_SDL_SPRAT_IMPORTED)
    set(SPRAT_SDL_INSTALL_DEFAULT ON)
else()
    set(SPRAT_SDL_INSTALL_DEFAULT OFF)
endif()
option(SPRAT_SDL_INSTALL "Generate install rules (needs installed SDL3 and sprat packages)" ${SPRAT_SDL_INSTALL_DEFAULT})

if(SPRAT_SDL_INSTALL)
    install(TARGETS sprat-sdl EXPORT sprat-sdl-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(FILES include/SDL3_sprat/SDL_sprat.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/SDL3_sprat)
    install(EXPORT sprat-sdl-targets
        NAMESPACE sprat-sdl::
        FILE sprat-sdl-targets.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sprat-sdl
    )
    include(CMakePackageConfigHelpers)
    configure_package_config_file(cmake/sprat-sdl-config.cmake.in
        ${PROJECT_BINARY_DIR}/sprat-sdl-config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sprat-sdl
    )
    write_basic_package_version_file(
        ${PROJECT_BINARY_DIR}/sprat-sdl-config-version.cmake
        COMPATIBILITY SameMajorVersion
    )
    install(FILES
        ${PROJECT_BINARY_DIR}/sprat-sdl-config.cmake
        ${PROJECT_BINARY_DIR}/sprat-sdl-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sprat-sdl
    )
endif()
