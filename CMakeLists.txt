cmake_minimum_required(VERSION 3.20)
project(MyGame VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# --- Components ---

# 1. Game Shared (Static Library)
# Contains logging, cvars, protos, common math
add_library(game_shared STATIC
    src/shared/log.hpp
    src/shared/cvar.hpp
    src/shared/timed_function.hpp
    src/shared/detached_console.hpp
    src/shared/replay_system.hpp
    src/shared/ecs.hpp
    src/shared/snapshot_system.hpp
    src/shared/task_system.hpp
    src/shared/task_system.cpp
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
target_link_libraries(game_shared PUBLIC libprotobuf)


# 2. Game Client (Shared Library / DLL)
# Contains Rendering (SDL/Vulkan), Input
add_library(game_client SHARED
    src/client/client_impl.cpp
    src/client/console.cpp
)
target_include_directories(game_client PUBLIC src/client)
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
    src/server/server_impl.cpp
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

