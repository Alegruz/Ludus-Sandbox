# Explicit Emscripten release payload. Add assets and corresponding notices here.
if(EMSCRIPTEN)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/index.html"
                  "$<TARGET_FILE:LudusSandbox>"
                  "$<TARGET_FILE_DIR:LudusSandbox>/index.wasm"
            DESTINATION . COMPONENT GameRelease)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/web/iframe.html" DESTINATION . COMPONENT GameRelease)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE.txt" DESTINATION . COMPONENT GameRelease)
    if(NOT DEFINED Ludus_SDK_MANIFEST)
        message(FATAL_ERROR "Release packaging requires an SDK identity manifest")
    endif()
    get_filename_component(_game_ludus_share "${Ludus_SDK_MANIFEST}" DIRECTORY)
    install(DIRECTORY "${_game_ludus_share}/licenses/" DESTINATION licenses/Ludus COMPONENT GameRelease)
    # Emscripten's cache may be relocated independently of its source tree.
    get_filename_component(_game_emcache "${EMSCRIPTEN_SYSROOT}" DIRECTORY)
    set(_game_webgpu "${_game_emcache}/ports/emdawnwebgpu/emdawnwebgpu_pkg/webgpu")
    foreach(_game_notice IN ITEMS
            "LICENSE|Emscripten.txt" "AUTHORS|Emscripten-authors.txt"
            "system/lib/libc/musl/COPYRIGHT|musl.txt"
            "system/lib/libcxx/LICENSE.TXT|libcxx.txt"
            "system/lib/libcxxabi/LICENSE.TXT|libcxxabi.txt"
            "system/lib/compiler-rt/LICENSE.TXT|compiler-rt.txt")
        string(REPLACE "|" ";" _game_parts "${_game_notice}")
        list(GET _game_parts 0 _game_source)
        list(GET _game_parts 1 _game_destination)
        install(FILES "${EMSCRIPTEN_ROOT_PATH}/${_game_source}"
                DESTINATION licenses RENAME "${_game_destination}" COMPONENT GameRelease)
    endforeach()
    install(FILES "${_game_webgpu}/src/LICENSE"
            DESTINATION licenses RENAME emdawnwebgpu.txt COMPONENT GameRelease)
    file(READ "${_game_webgpu}/include/webgpu/webgpu.h" _game_webgpu_header)
    string(FIND "${_game_webgpu_header}" "#ifndef WEBGPU_H_" _game_notice_end)
    if(_game_notice_end LESS 1 OR NOT _game_webgpu_header MATCHES "BSD 3-Clause License")
        message(FATAL_ERROR "Missing WebGPU header notice")
    endif()
    string(SUBSTRING "${_game_webgpu_header}" 0 ${_game_notice_end} _game_webgpu_notice)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/webgpu-native.txt" "${_game_webgpu_notice}")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/webgpu-native.txt"
            DESTINATION licenses COMPONENT GameRelease)
    get_filename_component(_game_cross_bin "${LUDUS_SPIRV_CROSS}" DIRECTORY)
    install(FILES "${_game_cross_bin}/../LICENSE"
            DESTINATION licenses RENAME SPIRV-Cross.txt COMPONENT GameRelease)
endif()
