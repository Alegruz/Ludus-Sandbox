cmake_minimum_required(VERSION 3.25)

project(
    LudusSandbox
    VERSION 0.1.0
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Consume Ludus only through its installed SDK/CMake package. No engine sources
# are vendored and no private/internal headers are included.
# ---------------------------------------------------------------------------

# Ludus::FoundationProfiling links Threads::Threads PUBLIC, but the installed
# LudusConfig.cmake does not find_dependency(Threads). Until the engine SDK adds
# that (see docs/VALIDATION.md "Known engine SDK packaging gap"), the consumer
# must resolve the Threads imported target itself, BEFORE find_package(Ludus) so
# the imported target exists when LudusTargets.cmake references it.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(Ludus CONFIG REQUIRED COMPONENTS Ui)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# GPU-free ocean logic shared by every target (settings, clock, uniforms,
# coordinate mapping). The adapter consumes the installed CPU fluid SDK; no GPU is required.
add_library(ludus_sandbox_ocean STATIC
    src/ocean/ocean_settings.cpp
    src/ocean/ocean_clock.cpp
    src/ocean/ocean_uniforms.cpp
    src/game/ripple_game.cpp
    src/game/water_field.cpp
    src/game/game_render.cpp
)
target_include_directories(ludus_sandbox_ocean PUBLIC src)
target_compile_features(ludus_sandbox_ocean PUBLIC cxx_std_23)
# When linked against the SDK, use the engine numeric aliases from the real
# <ludus/foundation/base/types.h>.
target_compile_definitions(ludus_sandbox_ocean PUBLIC LUDUS_SANDBOX_WITH_SDK)
target_link_libraries(ludus_sandbox_ocean PUBLIC Ludus::FoundationBase Ludus::FoundationMath Ludus::PhysicsFluid)
target_compile_options(ludus_sandbox_ocean PRIVATE -ffp-contract=off)
target_compile_options(ludus_sandbox_ocean PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions)

# ---------------------------------------------------------------------------
# The runnable ocean playground. Native uses the Vulkan backend; the browser
# build (Emscripten) uses Auto WebGPU/WebGL 2 selection and the DOM panel.
# ---------------------------------------------------------------------------
add_executable(LudusSandbox
    src/ocean/ocean_scene.cpp
    src/ui/game_hud.cpp
)
target_link_libraries(LudusSandbox PRIVATE
    ludus_sandbox_ocean
    Ludus::FoundationBase
    Ludus::FoundationLogging
    Ludus::FoundationProfiling
    Ludus::Platform
    Ludus::GraphicsRhi
    Ludus::Ui
)
target_compile_options(LudusSandbox PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions)

# Author-owned Slang ocean shader -> SPIR-V (native) + WGSL and GLSL ES (web) at build time.
# Requires pinned LUDUS_SLANG_COMPILER, LUDUS_SPIRV_VALIDATOR and
# LUDUS_SPIRV_CROSS host tools for the browser build.
ludus_compile_shader(
    TARGET LudusSandbox
    NAME ocean
    SOURCE shaders/ocean.slang
    VERTEX vertexMain
    FRAGMENT fragmentMain
    INCLUDES shaders
)

if(EMSCRIPTEN)
    target_sources(LudusSandbox PRIVATE src/main_web.cpp)
    target_compile_options(LudusSandbox PRIVATE "--use-port=emdawnwebgpu:cpp_bindings=false")
    target_link_options(LudusSandbox PRIVATE
        -fno-exceptions -g0 $<$<NOT:$<CONFIG:Release>>:-g2>
        -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432
        -sMAXIMUM_MEMORY=268435456 -sSTACK_SIZE=65536 -sASSERTIONS=1
        -sEXPORTED_RUNTIME_METHODS=['UTF8ToString','stringToUTF8','lengthBytesUTF8']
        -sEXPORTED_FUNCTIONS=['_main','_malloc','_free'])
    set_target_properties(LudusSandbox PROPERTIES OUTPUT_NAME index SUFFIX ".js")
    configure_file(web/shell.html index.html COPYONLY)
else()
    target_sources(LudusSandbox PRIVATE src/main_native.cpp)
endif()

# ---------------------------------------------------------------------------
# Focused host-side tests: settings validation, coordinate mapping, current/
# time continuity, and the uniform layout static asserts. Ocean tests are
# SDK-independent; gameplay tests use the production fluid SDK without a GPU.
# ---------------------------------------------------------------------------
option(LUDUS_SANDBOX_BUILD_TESTS "Build GPU-free ocean and SDK fluid gameplay tests" ON)
if(LUDUS_SANDBOX_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(ludus_sandbox_ocean_tests
        tests/ocean_tests.cpp
        src/ocean/ocean_settings.cpp
        src/ocean/ocean_clock.cpp
        src/ocean/ocean_uniforms.cpp
    )
    # The tests use the standalone numeric shim (no SDK dependency), exercising
    # the exact same source as the SDK build with identical widths.
    target_include_directories(ludus_sandbox_ocean_tests PRIVATE src)
    target_compile_features(ludus_sandbox_ocean_tests PRIVATE cxx_std_23)
    target_compile_options(ludus_sandbox_ocean_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions)
    # Fluid simulation is exercised through the installed production SDK.
    add_executable(ludus_sandbox_ripple_tests tests/ripple_tests.cpp)
    target_link_libraries(ludus_sandbox_ripple_tests PRIVATE ludus_sandbox_ocean)
    target_compile_options(ludus_sandbox_ripple_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions)
    enable_testing()
    add_executable(ludus_sandbox_ui_tests tests/ui_tests.cpp src/ui/game_hud.cpp)
    target_include_directories(ludus_sandbox_ui_tests PRIVATE src)
    target_link_libraries(ludus_sandbox_ui_tests PRIVATE ludus_sandbox_ocean Ludus::Ui)
    target_compile_options(ludus_sandbox_ui_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions)
    add_test(NAME ludus_sandbox_ui_tests COMMAND ludus_sandbox_ui_tests)
    add_test(NAME ludus_sandbox_ocean_tests COMMAND ludus_sandbox_ocean_tests)
    add_test(NAME ludus_sandbox_ripple_tests COMMAND ludus_sandbox_ripple_tests)
endif()

include(cmake/GameRelease.cmake)
