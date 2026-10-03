# Explicit Emscripten release payload. Add assets and corresponding notices here.
if(EMSCRIPTEN)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/index.html"
                  "$<TARGET_FILE:LudusSandbox>"
                  "$<TARGET_FILE_DIR:LudusSandbox>/index.wasm"
            DESTINATION . COMPONENT GameRelease)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE.txt" DESTINATION . COMPONENT GameRelease)
    if(NOT DEFINED Ludus_SDK_MANIFEST)
        message(FATAL_ERROR "Release packaging requires an SDK identity manifest")
    endif()
    get_filename_component(_game_ludus_share "${Ludus_SDK_MANIFEST}" DIRECTORY)
    install(DIRECTORY "${_game_ludus_share}/licenses/" DESTINATION licenses/Ludus COMPONENT GameRelease)
endif()
