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


# --- Host Tools ---

# The schema compiler: parses every .def file and emits the constexpr tables and
# plain structs the game reads. Standalone, no project dependencies. Inspect the
# parsed IR without writing anything with:
#   ./cmake_build/bin/def_gen src/shared/entities/entities.def src/shared/cvars/cvars.def --dump
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

set(CVAR_DEF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/shared/cvars/cvars.def)
set(CVAR_GEN_OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/shared/cvars/generated)
set(CVAR_GEN_HEADER ${CVAR_GEN_OUT_DIR}/cvars_generated.hpp)
set(CVAR_GEN_SOURCE ${CVAR_GEN_OUT_DIR}/cvars_generated.cpp)
# One binder TU per side, each compiled into the module that owns those
# handlers, so the client DLL never references a server handler symbol.
set(CVAR_GEN_SERVER_BINDINGS ${CVAR_GEN_OUT_DIR}/server_command_bindings.cpp)
set(CVAR_GEN_CLIENT_BINDINGS ${CVAR_GEN_OUT_DIR}/client_command_bindings.cpp)
file(MAKE_DIRECTORY ${CVAR_GEN_OUT_DIR})

# The .def's asset classes scan these directories, so the generated manifest
# depends on their CONTENTS, not just on the .def. CONFIGURE_DEPENDS re-runs the
# glob on build, so adding a mesh regenerates rather than silently producing a
# manifest that is missing it. Paths inside the .def are relative to
# --asset-root, which is why they can read the same way the game reads them.
file(GLOB ENTITY_GEN_ASSET_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/obj/*.obj
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/sprites/*.png
)

# ONE invocation over EVERY .def, not one per file. SCHEMA_HASH is computed
# across all of them and emitted once, so a per-file run would write a hash that
# disagrees with a full build.
add_custom_command(
    OUTPUT ${ENTITY_GEN_HEADER} ${ENTITY_GEN_SOURCE}
           ${CVAR_GEN_HEADER} ${CVAR_GEN_SOURCE}
           ${CVAR_GEN_SERVER_BINDINGS} ${CVAR_GEN_CLIENT_BINDINGS}
    COMMAND def_gen ${ENTITY_DEF_FILE} ${CVAR_DEF_FILE}
            --emit
            --asset-root ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS def_gen ${ENTITY_DEF_FILE} ${CVAR_DEF_FILE} ${ENTITY_GEN_ASSET_FILES}
    COMMENT "Generating definitions from entities.def and cvars.def..."
)


# --- Components ---

# 1. Game Shared (Static Library)
# Contains logging, cvars, protos, common math
add_library(game_shared STATIC
    src/shared/file_watcher.hpp
    src/shared/file_watcher.cpp
    src/shared/log.hpp
    src/shared/cvar.hpp
    src/shared/timed_function.hpp
    src/shared/detached_console.hpp
    src/shared/replay_system.hpp
    src/shared/old_ideas/ecs.hpp
    src/shared/snapshot_system.hpp
    src/shared/task_system.hpp
    src/shared/task_system.cpp
    ${ENTITY_GEN_SOURCE}
    # The cvar tables and text conversion. Holds no reference to any command
    # handler, so it compiles here without either side being present -- the two
    # binder TUs are what reference handlers, and they go into the DLLs that
    # own them (see game_client / game_server below).
    ${CVAR_GEN_SOURCE}
    src/shared/cvars/cvar_runtime.hpp
    src/shared/cvars/cvar_console.hpp
    src/shared/cvars/cvar_console.cpp
    src/shared/entities/entity_reflection.cpp
    src/shared/network/entity_serialization.cpp
    src/shared/network/entity_snapshot.cpp
    src/shared/network/udp_socket.cpp
    src/shared/collision_detection.cpp
    src/shared/debug_collision.cpp
    src/shared/components/components.cpp
    src/shared/map.cpp
    src/shared/map_geometry.cpp
    src/shared/map_baker.cpp
    src/shared/pathfinding.cpp
    src/shared/entity_system.cpp
    src/shared/game_session.cpp
    src/shared/asset.hpp
    src/shared/asset.cpp
    src/shared/player_move.cpp
    src/shared/physics.hpp
    src/shared/physics.cpp
    src/shared/bot_debug.hpp
    src/shared/bot_debug.cpp
    src/shared/cosmetic_events.hpp
    src/shared/cosmetic_events.cpp
    src/shared/game_events.hpp
    src/shared/game_events.cpp
    src/shared/network/map_transfer.cpp
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

set(SHADER_SOURCES
    ${SHADER_SRC_DIR}/aabb.vert
    ${SHADER_SRC_DIR}/aabb.frag
    ${SHADER_SRC_DIR}/unlit.vert
    ${SHADER_SRC_DIR}/unlit.frag
    ${SHADER_SRC_DIR}/lit.vert
    ${SHADER_SRC_DIR}/lit.frag
    ${SHADER_SRC_DIR}/particle.comp
    ${SHADER_SRC_DIR}/particle.vert
    ${SHADER_SRC_DIR}/particle.frag
    ${SHADER_SRC_DIR}/disp_textured.vert
    ${SHADER_SRC_DIR}/disp_textured.frag
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
    src/client/cosmetic_events.cpp
    src/client/effects/rocket_explosion.cpp
    src/client/effects/footstep.cpp
    src/client/effects/player_movement.cpp
    src/client/audio/audio_system.cpp
    src/client/audio/miniaudio_impl.cpp
    src/client/game_events.cpp
    src/client/hud/kill_feed.cpp
    src/client/console.cpp
    src/client/state_manager.cpp
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
    src/client/editor/entity_inspector.cpp
    src/client/editor/entity_editor_traits.cpp
    src/client/editor/geometry_editor.cpp
    src/client/input.cpp
    src/client/geometry_renderer.cpp
    src/client/renderer.cpp
    src/client/shader_tool_runtime.cpp
    ${SHADER_HEADERS}
)
target_include_directories(game_client PUBLIC
    src/client
    ${SHADER_OUT_DIR}
)
# miniaudio.h lives at the populated source root; only game_client needs it.
target_include_directories(game_client PRIVATE ${miniaudio_SOURCE_DIR})
target_link_libraries(game_client PUBLIC 
    game_shared 
    Vulkan::Vulkan 
    SDL2::SDL2-static 
    SDL2::SDL2main
    imgui
)
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
    src/server/cosmetic_events.cpp
    src/server/game_events.cpp
    src/server/damage.hpp
    src/server/damage.cpp
    src/server/trigger_actions.hpp
    src/server/trigger_actions.cpp
    src/server/systems/rocket_system.cpp
    src/server/systems/bot_system.cpp
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
target_link_libraries(MyGame PRIVATE game_client game_server game_shared)
# For macOS app bundle (optional, keeping as console app for now since detached_console used)


# 5. Dedicated Server (Server only)
add_executable(MyGame_Server src/launcher/main_dedicated.cpp)
target_include_directories(MyGame_Server PRIVATE src)
target_link_libraries(MyGame_Server PRIVATE game_server game_shared)


# 5b. Networked Client (Client only, connects to a separate MyGame_Server)
add_executable(MyGame_Client src/launcher/main_client.cpp)
target_include_directories(MyGame_Client PRIVATE src)
target_link_libraries(MyGame_Client PRIVATE game_client game_shared)


# 6. Task System Test
add_executable(task_system_test src/test/task_system_test.cpp)
target_include_directories(task_system_test PRIVATE src)
# Needs game_shared for the task system implementation
target_link_libraries(task_system_test PRIVATE game_shared)

# 7. Log Test
add_executable(log_test src/test/log_test.cpp)
target_include_directories(log_test PRIVATE src)
target_link_libraries(log_test PRIVATE game_shared)

# 8. ECS Test
add_executable(ecs_test src/test/ecs_test.cpp)
target_include_directories(ecs_test PRIVATE src)
target_link_libraries(ecs_test PRIVATE game_shared)
# 9. Camera Test
add_executable(camera_test src/test/camera_test.cpp)
target_include_directories(camera_test PRIVATE src)
# No link deps mostly, but game_client might be needed if camera.cpp exists later, 
# for now camera.hpp is header only.

# 10. Linalg Test
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
add_executable(entity_layout_test src/test/entity_layout_test.cpp ${ENTITY_GEN_SOURCE})
target_include_directories(entity_layout_test PRIVATE src/shared)


add_executable(map_migration_test src/test/map_migration_test.cpp)
target_include_directories(map_migration_test PRIVATE src src/shared)
target_link_libraries(map_migration_test PRIVATE game_shared)

# One-time map converter for the geometry exit (src/tools/map_convert.cpp).
# Loading a map converts it in memory; this tool makes that deliberate and
# batchable. Usage: map_convert [--check] maps/*.source
add_executable(map_convert src/tools/map_convert.cpp)
target_include_directories(map_convert PRIVATE src src/shared)
target_link_libraries(map_convert PRIVATE game_shared)
