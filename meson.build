project('MyGame', 'cpp', 'c',
  version : '0.1.0',
  default_options : ['warning_level=3', 'cpp_std=c++23']
)

# SDL2
cmake = import('cmake')
sdl2_opts = cmake.subproject_options()
sdl2_opts.add_cmake_defines({'SDL_STATIC': 'ON', 'SDL_SHARED': 'OFF', 'SDL_TEST': 'OFF'})
sdl2_proj = cmake.subproject('sdl2', options: sdl2_opts)
sdl2_lib = sdl2_proj.dependency('SDL2-static')

sdl2_deps = [sdl2_lib]
if host_machine.system() == 'darwin'
  frameworks = dependency('appleframeworks', modules : [
    'CoreAudio', 'AudioToolbox', 'CoreVideo', 'Cocoa', 'IOKit', 
    'Carbon', 'Metal', 'CoreHaptics', 'GameController', 'ForceFeedback'
  ])
  iconv = dependency('iconv')
  sdl2_deps += [frameworks, iconv]
endif

sdl2_dep = declare_dependency(dependencies: sdl2_deps)
meson.override_dependency('sdl2', sdl2_dep)

# Vulkan
vulkan_dep = dependency('vulkan', required : false)
if not vulkan_dep.found()
  # Try to find library manually, using VULKAN_SDK if available
  # Use python to get env var safely across platforms
  python_cmd = 'import os; print(os.environ.get("VULKAN_SDK", ""))'
  vulkan_sdk_path = run_command('python3', '-c', python_cmd, check: false).stdout().strip()
  
  vulkan_search_dirs = []
  if vulkan_sdk_path != ''
    if host_machine.system() == 'windows'
       vulkan_search_dirs += [vulkan_sdk_path / 'Lib']
    else
       vulkan_search_dirs += [vulkan_sdk_path / 'lib', vulkan_sdk_path / 'macOS/lib']
    endif
  endif
  
  if host_machine.system() != 'windows'
    # Also try /usr/local/lib for Homebrew on Unix-likes
    vulkan_search_dirs += ['/usr/local/lib', '/opt/homebrew/lib']
  endif

  cc = meson.get_compiler('cpp')
  if cc.get_id() == 'clang'
    add_project_arguments('-ftime-trace', language : 'cpp')
  endif
  vulkan_lib = cc.find_library('vulkan', dirs : vulkan_search_dirs, required : true)
  
  vulkan_inc = []
  if vulkan_sdk_path != ''
     if host_machine.system() == 'windows'
        vulkan_inc += include_directories(vulkan_sdk_path / 'Include')
     else
        vulkan_inc += include_directories(vulkan_sdk_path / 'include')
     endif
  endif
  if host_machine.system() == 'darwin'
    vulkan_inc += include_directories('/opt/homebrew/include')
  endif

  vulkan_dep = declare_dependency(
    dependencies : vulkan_lib,
    include_directories : vulkan_inc
  )
  meson.override_dependency('vulkan', vulkan_dep)
endif

if host_machine.system() == 'windows'
  add_project_arguments('-DNOMINMAX', language : 'cpp')
endif

# ImGui
imgui_dep = dependency('imgui')

# Protobuf
protobuf_dep = dependency('protobuf', required : false)
if protobuf_dep.found()
  protobuf_prog = find_program('protoc')
else
  # Protobuf using CMake module
  protobuf_opts = cmake.subproject_options()
  
  cxx_flags = '-w -Wno-error'
  if meson.get_compiler('cpp').get_id() == 'msvc'
    cxx_flags = '/w'
  endif

  protobuf_opts.add_cmake_defines({
    'protobuf_BUILD_TESTS': 'OFF', 
    'protobuf_BUILD_EXAMPLES': 'OFF',
    'ABSL_ENABLE_WERROR': 'OFF',
    'CMAKE_CXX_FLAGS': cxx_flags # Suppress warnings
  })
  protobuf_opts.set_override_option('cpp_std', 'c++17') 
  protobuf_proj = cmake.subproject('protobuf', options: protobuf_opts)
  protobuf_dep = protobuf_proj.dependency('libprotobuf')
  protobuf_prog = protobuf_proj.target('protoc')
endif

# 1. Game Shared
# Protobuf Generation via custom_target for better include control
protoc_gen = custom_target('game_proto',
  input : 'proto/game.proto',
  output : ['game.pb.cc', 'game.pb.h'],
  command : [protobuf_prog, '--cpp_out=@OUTDIR@', '-I@CURRENT_SOURCE_DIR@/proto', '@INPUT@']
)

game_shared_src = [
  'src/shared/log.hpp',
  'src/shared/cvar.hpp',
  'src/shared/timed_function.hpp',
  'src/shared/detached_console.hpp',
  'src/shared/replay_system.hpp',
  'src/shared/ecs.hpp',
  'src/shared/snapshot_system.hpp',
  'src/shared/task_system.hpp',
  'src/shared/task_system.cpp',
  protoc_gen
]

game_shared_inc = include_directories('src/shared', 'src')

game_shared_lib = shared_library('game_shared',
  game_shared_src,
  include_directories : game_shared_inc,
  dependencies : [protobuf_dep]
)

game_shared_dep = declare_dependency(
  link_with : game_shared_lib,
  include_directories : game_shared_inc,
  sources : protoc_gen[1], # Compile dependency (headers only)
  dependencies: [protobuf_dep]
)

# 2. Game Client
game_client_src = [
  'src/client/client_impl.cpp',
  'src/client/console.cpp',
  'src/client/state_manager.cpp',
  'src/client/states/play_state.cpp',
  'src/client/states/editor_state.cpp',
  'src/client/input.cpp',
  'src/client/renderer.cpp'
]

game_client_inc = include_directories('src/client')

# Shaders
glslc = find_program('glslc', required : false)
shader_targets = []
if glslc.found()
  shaders = [
    'src/client/shaders/aabb.vert',
    'src/client/shaders/aabb.frag'
  ]
  
  foreach s : shaders
    name = s.split('/')[-1]
    shader_targets += custom_target(
      name + '_spv',
      input : s,
      output : name + '.spv.h',
      command : [glslc, '@INPUT@', '-o', '@OUTPUT@', '--target-env=vulkan1.0', '-mfmt=c']
    )
  endforeach
endif

game_client_lib = shared_library('game_client',
  game_client_src,
  include_directories : [game_client_inc],
  dependencies : [game_shared_dep, vulkan_dep, sdl2_dep, imgui_dep],
  sources : shader_targets # Add shader headers dependency
)

game_client_dep = declare_dependency(
  link_with : game_client_lib,
  include_directories : game_client_inc
)

# 3. Game Server
game_server_src = [
  'src/server/server_impl.cpp'
]

game_server_inc = include_directories('src/server')

game_server_lib = shared_library('game_server',
  game_server_src,
  include_directories : [game_server_inc],
  dependencies : [game_shared_dep]
)

game_server_dep = declare_dependency(
  link_with : game_server_lib,
  include_directories : game_server_inc
)

# Executables

# 4. Integrated Game
executable('MyGame',
  'src/launcher/main_integrated.cpp',
  dependencies : [game_client_dep, game_server_dep, game_shared_dep],
  include_directories : include_directories('src')
)

# 5. Dedicated Server
executable('MyGame_Server',
  'src/launcher/main_dedicated.cpp',
  dependencies : [game_server_dep, game_shared_dep],
  include_directories : include_directories('src')
)

# Tests
executable('task_system_test',
  'src/test/task_system_test.cpp',
  dependencies : [game_shared_dep],
  include_directories : include_directories('src')
)

executable('log_test',
  'src/test/log_test.cpp',
  dependencies : [game_shared_dep],
  include_directories : include_directories('src')
)

executable('ecs_test',
  'src/test/ecs_test.cpp',
  dependencies : [game_shared_dep],
  include_directories : include_directories('src')
)


executable('camera_test',
  'src/test/camera_test.cpp',
  dependencies : [game_shared_dep],
  include_directories : include_directories('src')
)

executable('linalg_test',
  'src/test/linalg_test.cpp',
  dependencies : [game_shared_dep],
  include_directories : include_directories('src')
)
