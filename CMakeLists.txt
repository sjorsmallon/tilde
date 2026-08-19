cmake_minimum_required(VERSION 3.20)
project(MyGame VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-ftime-trace)
endif()
# Configure output directories for binaries (so DLLs and EXEs end up in same place)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- Dependencies ---

# 1. Vulkan
find_package(Vulkan REQUIRED)

# 2. SDL2
include(FetchContent)
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.28.5
)
set(SDL_SHARED OFF CACHE BOOL "Build SDL2 as a shared library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Build SDL2 as a static library" FORCE)
set(SDL_TEST OFF CACHE BOOL "Build SDL2 test" FORCE)
FetchContent_MakeAvailable(SDL2)

# 3. Protobuf
# this is to silence a shitload of float normalization warnings that are super annoying.
add_compile_definitions(_SILENCE_CXX23_DENORM_DEPRECATION_WARNING)

# CRT flags strncpy/getenv/sprintf etc. as deprecated in favor of the _s variants;
# this codebase uses the plain C functions deliberately, so silence that warning
# rather than adding _s calls or per-file pragmas.
add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)
set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    protobuf
    GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
    GIT_TAG v26.0
)
FetchContent_MakeAvailable(protobuf)

# 4. ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.1
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC
    SDL2::SDL2-static
    Vulkan::Vulkan
)

# 5. Jolt Physics
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.2.0
    SOURCE_SUBDIR Build  # Jolt's CMakeLists.txt lives in Build/, not the repo root
)
FetchContent_MakeAvailable(JoltPhysics)

# 6. miniaudio (single-header audio backend)
# Header-only: we don't use its CMakeLists (it builds tests/examples), just
# populate the source and compile the implementation in our own TU
# (src/client/audio/miniaudio_impl.cpp).
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.21
)
FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)
endif()

# this is to silence a lot of schema warnings.
if(MSVC)
    add_compile_options(/Zc:preprocessor)
endif()

# The exhaustive switch over a closed enum is this codebase's main safety net --
# adding an entity, an event or an effect is SUPPOSED to break the build at
# every site that has to learn about it (CLAUDE.md leans on this repeatedly). As
# a mere warning it was not that: PLAYER_DAMAGED sat half-serialized with this
# firing twice on every build and nobody reading it.
#
# Deliberately BELOW the FetchContent blocks: add_compile_options only reaches
# targets declared after it, so SDL2 / protobuf / miniaudio keep their own
# warning policy and a third-party switch cannot break our build.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Werror=switch)
elseif(MSVC)
    add_compile_options(/we4062) # enumerator not handled in switch
endif()


# --- Host Tools ---

# The schema compiler: parses every .def file and emits the constexpr tables and
# plain structs the game reads. Standalone, no project dependencies. Inspect the
# parsed IR without writing anything with:
#   ./cmake_build/bin/def_gen src/shared/entities/entities.def src/shared/assets/assets.def src/shared/cvars/cvars.def --dump
add_executable(def_gen src/tools/def_gen.cpp)

# Generated structs and tables. These land in the source tree rather than the
# build dir, on purpose: they are meant to be readable, opened without building,
# and to show up as a reviewable diff when a .def changes. Each .def's output
# goes to a `generated/` directory beside it, which def_gen derives from the
# input path -- the paths below must match that convention.
set(ENTITY_DEF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/shared/entities/entities.def)
set(ENTITY_GEN_OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/shared/entities/generated)
set(ENTITY_GEN_HEADER ${ENTITY_GEN_OUT_DIR}/entities_generated.hpp)
set(ENTITY_GEN_SOURCE ${ENTITY_GEN_OUT_DIR}/entities_generated.cpp)
file(MAKE_DIRECTORY ${ENTITY_GEN_OUT_DIR})

# The asset manifest. entities.def `import`s this file, so def_gen reads it
# twice in one run -- once as its own input (which is what emits it) and once
# through the import (which only resolves mesh_asset as a field type).
set(ASSET_DEF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/shared/assets/assets.def)
set(ASSET_GEN_OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/shared/assets/generated)
set(ASSET_GEN_HEADER ${ASSET_GEN_OUT_DIR}/assets_generated.hpp)
set(ASSET_GEN_SOURCE ${ASSET_GEN_OUT_DIR}/assets_generated.cpp)
file(MAKE_DIRECTORY ${ASSET_GEN_OUT_DIR})

set(CVAR_DEF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/shared/cvars/cvars.def)
set(CVAR_GEN_OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/shared/cvars/generated)
set(CVAR_GEN_HEADER ${CVAR_GEN_OUT_DIR}/cvars_generated.hpp)
set(CVAR_GEN_SOURCE ${CVAR_GEN_OUT_DIR}/cvars_generated.cpp)
# One binder TU per side, each compiled into the module that owns those
# handlers, so the client DLL never references a server handler symbol.
set(CVAR_GEN_SERVER_BINDINGS ${CVAR_GEN_OUT_DIR}/server_command_bindings.cpp)
set(CVAR_GEN_CLIENT_BINDINGS ${CVAR_GEN_OUT_DIR}/client_command_bindings.cpp)
file(MAKE_DIRECTORY ${CVAR_GEN_OUT_DIR})

# The two server->client dispatch channels, one .def each -- the event family is
# the first with two input files, so every emitted name is derived from the
# .def's own stem rather than hardcoded. One per-side binder TU each, for the
# cvar family's reason: it is what references the handlers, so it compiles into
# the module that defines them.
set(EFFECT_DEF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/shared/effects/effects.def)
set(EFFECT_GEN_OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/shared/effects/generated)
set(EFFECT_GEN_HEADER ${EFFECT_GEN_OUT_DIR}/effects_generated.hpp)
set(EFFECT_GEN_SOURCE ${EFFECT_GEN_OUT_DIR}/effects_generated.cpp)
set(EFFECT_GEN_CLIENT_BINDINGS ${EFFECT_GEN_OUT_DIR}/client_effects_bindings.cpp)
file(MAKE_DIRECTORY ${EFFECT_GEN_OUT_DIR})

set(EVENT_DEF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/shared/events/events.def)
set(EVENT_GEN_OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/shared/events/generated)
set(EVENT_GEN_HEADER ${EVENT_GEN_OUT_DIR}/events_generated.hpp)
set(EVENT_GEN_SOURCE ${EVENT_GEN_OUT_DIR}/events_generated.cpp)
set(EVENT_GEN_CLIENT_BINDINGS ${EVENT_GEN_OUT_DIR}/client_events_bindings.cpp)
file(MAKE_DIRECTORY ${EVENT_GEN_OUT_DIR})

# assets.def's classes scan these directories, so the generated manifest
# depends on their CONTENTS, not just on the .def. CONFIGURE_DEPENDS re-runs the
# glob on build, so adding a mesh regenerates rather than silently producing a
# manifest that is missing it. Paths inside the .def are relative to
# --asset-root, which is why they can read the same way the game reads them.
file(GLOB ASSET_GEN_SCANNED_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/obj/*.obj
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/models/*.mesh
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/sprites/*.png
)

# ONE invocation over EVERY .def, not one per file. SCHEMA_HASH is computed
# across all of them and emitted once, so a per-file run would write a hash that
# disagrees with a full build.
add_custom_command(
    OUTPUT ${ENTITY_GEN_HEADER} ${ENTITY_GEN_SOURCE}
           ${ASSET_GEN_HEADER} ${ASSET_GEN_SOURCE}
           ${CVAR_GEN_HEADER} ${CVAR_GEN_SOURCE}
           ${CVAR_GEN_SERVER_BINDINGS} ${CVAR_GEN_CLIENT_BINDINGS}
           ${EFFECT_GEN_HEADER} ${EFFECT_GEN_SOURCE} ${EFFECT_GEN_CLIENT_BINDINGS}
           ${EVENT_GEN_HEADER} ${EVENT_GEN_SOURCE} ${EVENT_GEN_CLIENT_BINDINGS}
    COMMAND def_gen ${ENTITY_DEF_FILE} ${ASSET_DEF_FILE} ${CVAR_DEF_FILE}
            ${EFFECT_DEF_FILE} ${EVENT_DEF_FILE}
            --emit
            --asset-root ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS def_gen ${ENTITY_DEF_FILE} ${ASSET_DEF_FILE} ${CVAR_DEF_FILE}
            ${EFFECT_DEF_FILE} ${EVENT_DEF_FILE}
            ${ASSET_GEN_SCANNED_FILES}
    COMMENT "Generating definitions from entities.def, assets.def, cvars.def, effects.def and events.def..."
)


# --- Components ---

# 1. Game Shared (Static Library)
# Contains logging, cvars, protos, common math
add_library(game_shared STATIC
    src/shared/file_watcher.hpp
    src/shared/file_watcher.cpp
    src/shared/log.hpp
    src/shared/timed_function.hpp
    src/shared/detached_console.hpp
    src/shared/replay_system.hpp
    src/shared/task_system.hpp
    src/shared/task_system.cpp
    ${ASSET_GEN_SOURCE}
    ${ENTITY_GEN_SOURCE}
    # The cvar tables and text conversion. Holds no reference to any command
    # handler, so it compiles here without either side being present -- the two
    # binder TUs are what reference handlers, and they go into the DLLs that
    # own them (see game_client / game_server below).
    ${CVAR_GEN_SOURCE}
    src/shared/cvars/cvar_runtime.hpp
    src/shared/cvars/cvar_console.hpp
    src/shared/cvars/cvar_console.cpp
    # The family-neutral reflection layer: the record types every generated
    # table is made of, the text conversion, and the wire codec for one field.
    # Entities and both event channels walk all three.
    src/shared/reflection.hpp
    src/shared/reflection.cpp
    src/shared/network/field_codec.hpp
    src/shared/network/field_codec.cpp
    src/shared/entities/entity_reflection.cpp
    src/shared/network/entity_serialization.cpp
    src/shared/network/entity_snapshot.cpp
    src/shared/network/udp_socket.cpp
    src/shared/collision_detection.cpp
    src/shared/debug_collision.cpp
    src/shared/map.cpp
    src/shared/map_geometry.cpp
    src/shared/map_baker.cpp
    src/shared/pathfinding.cpp
    src/shared/entity_system.cpp
    src/shared/game_session.cpp
    src/shared/asset.hpp
    src/shared/asset.cpp
    src/shared/skeleton.hpp
    src/shared/model_format.hpp
    src/shared/model_format.cpp
    src/shared/skinning.hpp
    src/shared/skinning.cpp
    src/shared/animation.hpp
    src/shared/animation.cpp
    src/shared/hitbox_rig.hpp
    src/shared/hitbox_rig.cpp
    src/shared/player_animator.hpp
    src/shared/player_animator.cpp
    src/shared/player_rig.hpp
    src/shared/player_rig.cpp
    src/shared/player_move.cpp
    src/shared/physics.hpp
    src/shared/physics.cpp
    # Both channels' tables and fire helpers. Neither references a handler, so
    # they compile here with neither side present -- the two binder TUs are what
    # reference them, and those go into game_client below.
    ${EFFECT_GEN_SOURCE}
    ${EVENT_GEN_SOURCE}
    src/shared/event_stream.hpp
    src/shared/event_stream.cpp
    src/shared/weapons.hpp
    src/shared/hit_region.hpp
    src/shared/hitscan.hpp
    src/shared/hitscan.cpp
    src/shared/lag_compensation.hpp
    src/shared/lag_compensation.cpp
    src/shared/network/map_transfer.cpp
    src/shared/network/cvar_mirror.cpp
)

# Protobuf Generation
if(TARGET protoc)
    set(PROTOC_EXECUTABLE protoc)
else()
    find_program(PROTOC_EXECUTABLE protoc REQUIRED)
endif()

set(PROTO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(PROTO_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_OUT_DIR})
set(PROTO_FILES ${PROTO_SRC_DIR}/game.proto)
set(PROTO_HDRS ${PROTO_OUT_DIR}/game.pb.h)
set(PROTO_SRCS ${PROTO_OUT_DIR}/game.pb.cc)

add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS}
    COMMAND ${PROTOC_EXECUTABLE}
    ARGS --cpp_out=${PROTO_OUT_DIR} -I${PROTO_SRC_DIR} ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating Protobuf sources..."
)

target_sources(game_shared PRIVATE ${PROTO_SRCS} ${PROTO_HDRS})
target_include_directories(game_shared PUBLIC 
    src/shared 
    ${PROTO_OUT_DIR}
)
target_link_libraries(game_shared PUBLIC libprotobuf Jolt)
target_include_directories(game_shared PUBLIC ${joltphysics_SOURCE_DIR})


# 2. Game Client (Shared Library / DLL)
# Contains Rendering (SDL/Vulkan), Input

# Shaders
find_program(GLSLC_EXECUTABLE glslc REQUIRED)
set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders)
set(SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})

# Two vertex shaders x two fragment shaders is the whole mesh family; the
# pipeline cache combines them with blend/cull/depth/fill. debug.* backs every
# debug primitive, and ui.* every screen-space quad (glyphs and solid rects
# alike). Anything under preview/ or pbr/ is compiled at RUNTIME by the shader
# tool and does not belong here.
set(SHADER_SOURCES
    ${SHADER_SRC_DIR}/debug.vert
    ${SHADER_SRC_DIR}/debug.frag
    ${SHADER_SRC_DIR}/mesh.vert
    ${SHADER_SRC_DIR}/mesh_skinned.vert
    ${SHADER_SRC_DIR}/mesh_lit.frag
    ${SHADER_SRC_DIR}/mesh_unlit.frag
    ${SHADER_SRC_DIR}/particle.comp
    ${SHADER_SRC_DIR}/particle.vert
    ${SHADER_SRC_DIR}/particle.frag
    ${SHADER_SRC_DIR}/ui.vert
    ${SHADER_SRC_DIR}/ui.frag
)
set(SHADER_HEADERS "")

foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(HEADER_OUT ${SHADER_OUT_DIR}/${SHADER_NAME}.spv.h)
    list(APPEND SHADER_HEADERS ${HEADER_OUT})
    
    add_custom_command(
        OUTPUT ${HEADER_OUT}
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER} -o ${HEADER_OUT} --target-env=vulkan1.0 -mfmt=c
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${SHADER_NAME} to C header..."
    )
endforeach()

# Shader tool paths — generated header so runtime knows where glslc and preview shaders live
set(PREVIEW_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders/preview")
set(SHADER_TOOL_CACHE_DIR "${CMAKE_CURRENT_BINARY_DIR}/shader_tool_cache")
file(MAKE_DIRECTORY ${SHADER_TOOL_CACHE_DIR})
configure_file(
    src/client/shader_tool_paths.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/shader_tool_paths.h
    @ONLY
)

add_library(game_client SHARED
    # Fills the @Client command slots. Compiled HERE, into the module that owns
    # those handlers, so this DLL never references a server handler symbol --
    # and a missing/misspelled @Client handler is a link error on this target.
    ${CVAR_GEN_CLIENT_BINDINGS}
    src/client/jolt_debug_renderer.cpp
    src/client/client_impl.cpp
    src/client/client_context.cpp
    # One binder TU per channel, each switching over its closed enum and calling
    # one hand-written function per member. A member with no function is a link
    # error on this target, naming the symbol.
    ${EFFECT_GEN_CLIENT_BINDINGS}
    ${EVENT_GEN_CLIENT_BINDINGS}
    src/client/event_handlers.hpp
    src/client/effects/rocket_explosion.cpp
    src/client/effects/footstep.cpp
    src/client/effects/jump.cpp
    src/client/effects/land.cpp
    src/client/effects/bullet_impact.cpp
    src/client/effects/flesh_impact.cpp
    src/client/weapon_fire_audio.cpp
    src/client/hit_confirm_audio.cpp
    src/client/audio/audio_system.cpp
    src/client/audio/miniaudio_impl.cpp
    src/client/game_events/rocket_detonated.cpp
    src/client/game_events/player_died.cpp
    src/client/game_events/player_spawned.cpp
    src/client/game_events/round_phase_changed.cpp
    src/client/hud/kill_feed.cpp
    src/client/hud/crosshair.cpp
    src/client/hud/announcement.cpp
    # The GPU-free halves of the UI layer, each its own TU so ui_test can
    # compile them without a device (see debug_draw_list.cpp, same reason).
    # ui_input.cpp is the exception and is deliberately NOT in that test: it is
    # the one file that touches SDL, which is what keeps the other four pure.
    src/client/ui_draw_list.cpp
    src/client/ui/font.cpp
    src/client/ui/screen.cpp
    src/client/ui/navigation.cpp
    src/client/ui/list_menu.cpp
    src/client/ui/ui_input.cpp
    src/client/console.cpp
    src/client/state_manager.cpp
    src/client/states/main_menu_state.cpp
    src/client/states/pause_menu.cpp
    src/client/states/play_state.cpp
    src/client/states/editor_gizmo.cpp
    src/client/states/tool_editor_state.cpp
    src/client/states/shader_editor_state.cpp
    src/client/editor/tools/selection_tool.cpp
    src/client/editor/tools/placement_tool.cpp
    src/client/editor/tools/sculpting_tool.cpp
    src/client/editor/tools/pathfinding_test_tool.cpp
    src/client/editor/tools/particle_editor_tool.cpp
    src/client/editor/tools/displacement_tool.cpp
    src/client/editor/tools/animation_tool.cpp
    src/client/editor/entity_inspector.cpp
    src/client/editor/entity_editor_traits.cpp
    src/client/editor/geometry_editor.cpp
    src/client/input.cpp
    src/client/geometry_renderer.cpp
    src/client/render_assets.cpp
    src/client/debug_draw_list.cpp
    src/client/hitbox_debug_draw.hpp
    src/client/renderer.cpp
    src/client/shader_tool_runtime.cpp
    ${SHADER_HEADERS}
)
target_include_directories(game_client PUBLIC
    src/client
    ${SHADER_OUT_DIR}
)
# So a test can link the editor tools. The DLL's own entry points are explicitly
# exported; everything else was internal, which is why the editor
# compile-isolation test sat uncompiled for so long.
set_target_properties(game_client PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
# miniaudio.h lives at the populated source root; only game_client needs it.
target_include_directories(game_client PRIVATE ${miniaudio_SOURCE_DIR})
target_link_libraries(game_client PUBLIC 
    game_shared 
    Vulkan::Vulkan 
    SDL2::SDL2-static 
    imgui
)
# SDL2main is an EXECUTABLE entry-point shim, so it belongs to the launchers
# rather than to the DLL they load. Propagating it from here put a second `main`
# into the link of anything that touched game_client -- which is what stopped a
# test from linking the editor tools.
if(APPLE)
    # Ensure RPATH setup for finding shared libs
    set_target_properties(game_client PROPERTIES MACOSX_RPATH TRUE)
elseif(WIN32)
    # Windows DLL export definitons usually needed, but for now we might expose C-API
    target_compile_definitions(game_client PRIVATE GAME_CLIENT_EXPORTS)
endif()


# 3. Game Server (Shared Library / DLL)
# Contains Game Logic
add_library(game_server SHARED
    # Fills the @Server command slots -- the other half of the split above.
    ${CVAR_GEN_SERVER_BINDINGS}
    src/server/server_impl.cpp
    # The reset seams: what is cleared on a map change, on a slot changing
    # occupant, and at each end of a tick. server_context_test guards them.
    src/server/server_context.hpp
    src/server/server_context.cpp
    src/server/bot_state.hpp
    src/server/damage.hpp
    src/server/damage.cpp
    src/server/entity_lifecycle.hpp
    src/server/entity_lifecycle.cpp
    src/server/trigger_actions.hpp
    src/server/trigger_actions.cpp
    src/server/systems/rocket_system.cpp
    src/server/systems/bot_system.cpp
    src/server/systems/game_rules_system.cpp
    src/server/systems/physics_body_system.cpp
    src/server/systems/respawn_system.cpp
)
target_include_directories(game_server PUBLIC src/server)
target_link_libraries(game_server PUBLIC game_shared)
if(WIN32)
    target_compile_definitions(game_server PRIVATE GAME_SERVER_EXPORTS)
endif()


# --- Executables ---

# 4. Integrated Game (Client + Server)
add_executable(MyGame src/launcher/main_integrated.cpp)
target_include_directories(MyGame PRIVATE src)
target_link_libraries(MyGame PRIVATE game_client game_server game_shared SDL2::SDL2main)
# For macOS app bundle (optional, keeping as console app for now since detached_console used)


# 5. Dedicated Server (Server only)
add_executable(MyGame_Server src/launcher/main_dedicated.cpp)
target_include_directories(MyGame_Server PRIVATE src)
target_link_libraries(MyGame_Server PRIVATE game_server game_shared)


# 5b. Networked Client (Client only, connects to a separate MyGame_Server)
add_executable(MyGame_Client src/launcher/main_client.cpp)
target_include_directories(MyGame_Client PRIVATE src)
target_link_libraries(MyGame_Client PRIVATE game_client game_shared SDL2::SDL2main)


# 6. Task System Test
add_executable(task_system_test src/test/task_system_test.cpp)
target_include_directories(task_system_test PRIVATE src)
# Needs game_shared for the task system implementation
target_link_libraries(task_system_test PRIVATE game_shared)

# 7. Log Test
add_executable(log_test src/test/log_test.cpp)
target_include_directories(log_test PRIVATE src)
target_link_libraries(log_test PRIVATE game_shared)

# 9. Camera Test
add_executable(camera_test src/test/camera_test.cpp)
target_include_directories(camera_test PRIVATE src)
# No link deps mostly, but game_client might be needed if camera.cpp exists later, 
# for now camera.hpp is header only.

# 10. Linalg Test
add_executable(hitscan_test src/test/hitscan_test.cpp)
target_include_directories(hitscan_test PRIVATE src)
target_link_libraries(hitscan_test PRIVATE game_shared)

add_executable(linalg_test src/test/linalg_test.cpp)
target_include_directories(linalg_test PRIVATE src)
 target_link_libraries(linalg_test PRIVATE game_shared)

# 11. File Watcher Test
add_executable(file_watcher_test src/test/file_watcher_test.cpp)
target_include_directories(file_watcher_test PRIVATE src)
target_link_libraries(file_watcher_test PRIVATE game_shared)

# 12. Network Serialization Test
add_executable(network_test src/test/test_network_serialization.cpp)
target_include_directories(network_test PRIVATE src)
target_link_libraries(network_test PRIVATE game_shared)

# 13. UDP Socket Test
add_executable(udp_socket_test src/test/test_udp_socket.cpp)
target_include_directories(udp_socket_test PRIVATE src)
target_link_libraries(udp_socket_test PRIVATE game_shared)

# 14. Server Loop Test
add_executable(server_loop_test src/test/server_loop_test.cpp)
target_include_directories(server_loop_test PRIVATE src)
target_link_libraries(server_loop_test PRIVATE game_shared)

# 15. Session Test
add_executable(session_test src/test/session_test.cpp)
target_include_directories(session_test PRIVATE src)
target_link_libraries(session_test PRIVATE game_shared)

# 16. Transaction System Test
# 16. Transaction System Test
add_executable(transaction_system_test src/test/test_transaction_system.cpp)
target_include_directories(transaction_system_test PRIVATE src src/client src/client/editor)
target_link_libraries(transaction_system_test PRIVATE game_shared Vulkan::Vulkan)



# 16b. Debug draw list — the renderer's one piece of GPU-free logic. It links
# the split translation unit rather than the client DLL, which is the point of
# the split: retire()'s lifetimes and the polygon pool are testable without a
# device, a swapchain or a window.
add_executable(debug_draw_list_test
    src/test/debug_draw_list_test.cpp
    src/client/debug_draw_list.cpp)
target_include_directories(debug_draw_list_test PRIVATE src src/client)
target_link_libraries(debug_draw_list_test PRIVATE game_shared Vulkan::Vulkan SDL2::SDL2-static)

# 16d. The UI layer's GPU-free halves: the font bake against the REAL
# resources/fonts TTF, ui_draw_list_t's batching, the layout anchors, and the
# whole retained-tree interaction model -- resolution, tweens and directional
# navigation. Same trick as 16b: it compiles the split TUs rather than linking
# game_client, so every glyph metric is checked with no device and no window.
#
# ui/ui_input.cpp is POINTEDLY absent. It is the only file in the layer that
# reads SDL, and everything downstream takes a ui_input_t by value -- so leaving
# it out is what proves the interaction model needs no window to test.
add_executable(ui_test
    src/test/ui_test.cpp
    src/client/ui_draw_list.cpp
    src/client/ui/font.cpp
    src/client/ui/screen.cpp
    src/client/ui/navigation.cpp
    src/client/ui/list_menu.cpp)
target_include_directories(ui_test PRIVATE src src/client)
target_link_libraries(ui_test PRIVATE game_shared Vulkan::Vulkan SDL2::SDL2-static)

# 16c. Editor tools must compile and LINK against pass_builder_t with no mock --
# which is the whole argument for having deleted overlay_renderer_t rather than
# reimplementing it. Built, but deliberately NOT in GAME_TESTS: see the file's
# header comment for why running it would test the module layout instead.
add_executable(editor_compile_test src/client/editor/test_editor_compile.cpp)
target_include_directories(editor_compile_test PRIVATE src src/client src/client/editor)
target_link_libraries(editor_compile_test PRIVATE game_client game_shared)

# 17. Entity Delta Packing Test
add_executable(test_entity_delta_packing src/test/test_entity_delta_packing.cpp)
target_include_directories(test_entity_delta_packing PRIVATE src)
target_link_libraries(test_entity_delta_packing PRIVATE game_shared)

# 17b. Snapshot Delta Test (acked-baseline rule + changed-field mask)
add_executable(snapshot_delta_test src/test/test_snapshot_delta.cpp)
target_include_directories(snapshot_delta_test PRIVATE src)
target_link_libraries(snapshot_delta_test PRIVATE game_shared)

# 18. RNG Test
add_executable(test_rng src/test/test_rng.cpp)
target_include_directories(test_rng PRIVATE src)
target_link_libraries(test_rng PRIVATE game_shared)

# 19. Asset System Test
add_executable(asset_test src/test/test_asset_system.cpp)
target_include_directories(asset_test PRIVATE src)
target_link_libraries(asset_test PRIVATE game_shared)

# 20. Navmesh Simplification Test
add_executable(navmesh_test src/test/test_navmesh.cpp)
target_include_directories(navmesh_test PRIVATE src)
target_link_libraries(navmesh_test PRIVATE game_shared)

# 21. Map migration / round-trip test (box_volume_t refactor + wedge-strip)
# Guards the generated entity layout: trivial copyability (memcpy snapshots and
# memcmp diffing) and derived-to-base conversion, which the inheritance layout
# was chosen to preserve.
add_executable(entity_layout_test src/test/entity_layout_test.cpp ${ENTITY_GEN_SOURCE}
                                  ${ASSET_GEN_SOURCE})
target_include_directories(entity_layout_test PRIVATE src/shared)


add_executable(map_migration_test src/test/map_migration_test.cpp)
target_include_directories(map_migration_test PRIVATE src src/shared)
target_link_libraries(map_migration_test PRIVATE game_shared)

# 22. CVar / command test — tables, text conversion, console dispatch,
# argument binders, and the @Mirrored wire path.
#
# It compiles BOTH generated binder TUs and defines its own commands::<name>
# handlers, which is the only way to exercise the binders outside the DLLs that
# own the real ones — and a standing check that a binder TU references nothing
# but those handler symbols.
add_executable(cvar_test src/test/cvar_test.cpp
    ${CVAR_GEN_SERVER_BINDINGS} ${CVAR_GEN_CLIENT_BINDINGS})
target_include_directories(cvar_test PRIVATE src src/shared)
target_link_libraries(cvar_test PRIVATE game_shared)

# 23. Model format test — the .skeleton / .mesh readers against the REAL
# exporter output in resources/models, plus malformed fixtures. This is where
# blender_export.py's promises (parent-before-child, name hash, normalized
# weights, engine-unit scale) and model_format.cpp's checks meet.
add_executable(model_format_test src/test/test_model_format.cpp)
target_include_directories(model_format_test PRIVATE src src/shared)
target_link_libraries(model_format_test PRIVATE game_shared)

# 24. Hitbox rig test — the `.hitboxes` reader/writer and the bone-span math,
# against the authored resources/models/rig.hitboxes. Holds the two §4 numbers
# that stopped being structural guarantees: hull excursion under the aim poses,
# and skin coverage. `hitbox_rig_test --dump` prints the derived radii the
# authored ones were seeded from.
add_executable(hitbox_rig_test src/test/hitbox_rig_test.cpp)
target_include_directories(hitbox_rig_test PRIVATE src src/shared)
target_link_libraries(hitbox_rig_test PRIVATE game_shared)

# 24b. Lag compensation — the rewind path (shared/lag_compensation.cpp).
# Links game_shared alone: the bracket check is pure arithmetic and the posing
# half takes values, so no server_context_t, no socket and no Jolt. Loads the
# real rig from resources/models, so it wants the project root as its cwd.
add_executable(lag_compensation_test src/test/lag_compensation_test.cpp)
target_include_directories(lag_compensation_test PRIVATE src src/shared)
target_link_libraries(lag_compensation_test PRIVATE game_shared)

# 25. Array test — Array<T, N> and Enum_Array<Enum_T, T> (src/shared/array.hpp).
# Header-only, so it links nothing: almost every promise these types make is a
# static_assert, and the executable exists to prove the header compiles and to
# hold the handful of checks that need real indexing.
add_executable(array_test src/test/array_test.cpp)
target_include_directories(array_test PRIVATE src src/shared)

# 26. Server context test — the reset seams in src/server/server_context.cpp.
# Asserts BOTH halves of each reset: what is cleared and what deliberately
# survives. The half that survives is the one a future edit is likely to get
# wrong.
#
# Compiles the two TUs it needs rather than linking game_server, the same way
# cvar_test compiles the generated binders: game_server is a SHARED lib and only
# its GAME_SERVER_API surface is exported, and the reset functions are internal
# to the module by design. This also keeps the test from loading a DLL to check
# what is really pure struct manipulation.
add_executable(server_context_test src/test/server_context_test.cpp
    src/server/server_context.cpp
    src/server/systems/game_rules_system.cpp)
target_include_directories(server_context_test PRIVATE src src/shared src/server)
target_link_libraries(server_context_test PRIVATE game_shared)

# 27. Player move step invariance — one tick of dt vs N sub-steps of dt/N.
# Step 2 of subtick_plan.md, and the prerequisite for sub-tick rather than a
# nicety: it is what classifies each divergence as fixable arithmetic or a
# deliberate clamp, by elimination. Also the only coverage player_move has.
#
# Links game_shared alone and builds its own one-box bvh, so no map, no session
# and no assets — player_move takes static geometry and a cvar_state_t and
# nothing else, which is exactly why it can be tested this cheaply.
add_executable(player_move_step_invariance_test
    src/test/player_move_step_invariance_test.cpp)
target_include_directories(player_move_step_invariance_test PRIVATE src src/shared)
target_link_libraries(player_move_step_invariance_test PRIVATE game_shared)

# 28. Move budget — the per-client movement rate bound (src/server/move_budget.hpp).
# Header-only, so it links nothing. The cases are multi-tick SIMULATIONS rather
# than unit checks because the property being guarded (neutrality to arrival
# pattern) is invisible inside any single tick.
add_executable(move_budget_test src/test/move_budget_test.cpp)
target_include_directories(move_budget_test PRIVATE src src/shared)

# One-time map converter for the geometry exit (src/tools/map_convert.cpp).
# Loading a map converts it in memory; this tool makes that deliberate and
# batchable. Usage: map_convert [--check] maps/*.source
add_executable(map_convert src/tools/map_convert.cpp)
target_include_directories(map_convert PRIVATE src src/shared)
target_link_libraries(map_convert PRIVATE game_shared)

# --- The test suite, as one command ------------------------------------------
#
# `ctest --test-dir cmake_build` runs everything below. The list is written out
# rather than globbed on purpose: a glob over bin/ would also catch MyGame,
# def_gen and map_convert, and the exclusion list needed to keep them out rots
# faster than this does.
#
# WORKING_DIRECTORY is the load-bearing part. map_migration_test loads
# `maps/test` by a path relative to the project root, so running it from
# anywhere else failed — a standing footgun that this pins shut for every test
# at once, rather than documenting it again.
enable_testing()

# 24. Event Family Round Trip
#
# fire -> encode -> decode -> compare for every declared member of both
# channels, plus the byte-for-byte check against the hand-written codec the
# generator replaced. cvar_test's counterpart for the event family.
add_executable(events_test src/test/test_events.cpp)
target_include_directories(events_test PRIVATE src)
target_link_libraries(events_test PRIVATE game_shared)

set(GAME_TESTS
    task_system_test
    log_test
    camera_test
    linalg_test
    file_watcher_test
    network_test
    udp_socket_test
    server_loop_test
    session_test
    transaction_system_test
    debug_draw_list_test
    test_entity_delta_packing
    snapshot_delta_test
    test_rng
    asset_test
    entity_layout_test
    map_migration_test
    navmesh_test
    cvar_test
    hitscan_test
    model_format_test
    hitbox_rig_test
    lag_compensation_test
    array_test
    server_context_test
    events_test
    ui_test
    player_move_step_invariance_test
    move_budget_test)

foreach(test_target IN LISTS GAME_TESTS)
  add_test(NAME ${test_target} COMMAND ${test_target})
  set_tests_properties(${test_target} PROPERTIES
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()
