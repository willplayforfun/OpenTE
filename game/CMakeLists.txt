cmake_minimum_required(VERSION 3.21)

project(OpenTE
    VERSION 0.1.0
    DESCRIPTION "An open-source clone of Trade Empires (2001)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_executable(opente
    src/main.cpp
    src/core/app.cpp
    src/core/paths.cpp
    src/core/scene_manager.cpp
    src/gameplay/gameplay_scene.cpp
    src/data/registry.cpp
    src/render/font.cpp
    src/render/font_cache.cpp
    src/render/terrain_renderer.cpp
    src/render/terrain_tileset.cpp
    src/render/texture.cpp
    src/world/region.cpp
    src/world/world.cpp
    src/ui/panel.cpp
    src/ui/manager.cpp
    src/ui/build_menu.cpp
)

target_include_directories(opente PRIVATE src)

target_link_libraries(opente PRIVATE
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
    nlohmann_json::nlohmann_json
    imgui::imgui
)

# Place the executable directly in the build output directory (no per-config
# subfolder) so the `game_data/` directory produced by the extractor can sit
# next to it at a predictable relative path during development.
set_target_properties(opente PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin"
)

# On Windows, vcpkg-built SDL2/SDL2_image are shared DLLs; copy them next to
# the executable so it runs without manually fiddling with PATH.
if(WIN32)
    add_custom_command(TARGET opente POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:opente>
            $<TARGET_FILE_DIR:opente>
        COMMAND_EXPAND_LISTS
    )
endif()

install(TARGETS opente RUNTIME DESTINATION bin)
if(WIN32)
    install(FILES $<TARGET_RUNTIME_DLLS:opente> DESTINATION bin)
endif()
